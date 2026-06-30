"use strict";

const crypto = require("crypto");
const fs = require("fs");
const https = require("https");
const net = require("net");
const path = require("path");
const Database = require("better-sqlite3");

const PORT = Number(process.env.BLINQ_SERVER_PORT || 45476);
const DATA_DIR = process.env.BLINQ_DATA_DIR || "/opt/blinq-server/data";
const UPLOAD_DIR = process.env.BLINQ_UPLOAD_DIR || "/opt/blinq-server/uploads";
const FIREBASE_SERVICE_ACCOUNT_PATH = process.env.BLINQ_FIREBASE_SERVICE_ACCOUNT || "/opt/blinq-server/firebase-service-account.json";
const SQLITE_PATH = path.join(DATA_DIR, "blinq.sqlite3");
const LEGACY_JSON_PATH = path.join(DATA_DIR, "db.json");
const DOMAIN = "blinqm.net";
const MAX_TEXT_LENGTH = 4000;
const MAX_IMAGE_BYTES = 5 * 1024 * 1024;
const MAX_LINE_BYTES = 8 * 1024 * 1024;
const UPLOAD_TTL_MS = Number(process.env.BLINQ_UPLOAD_TTL_MS || 60 * 60 * 1000);
const QUEUED_MESSAGE_TTL_MS = Number(process.env.BLINQ_QUEUED_MESSAGE_TTL_MS || 7 * 24 * 60 * 60 * 1000);
const MAX_QUEUED_MESSAGES_PER_USER = Number(process.env.BLINQ_MAX_QUEUED_MESSAGES_PER_USER || 200);
const MAX_QUEUED_TEXT_BYTES_PER_USER = Number(process.env.BLINQ_MAX_QUEUED_TEXT_BYTES_PER_USER || 1024 * 1024);
const PASSWORD_RESET_CODE_TTL_MS = Number(process.env.BLINQ_PASSWORD_RESET_CODE_TTL_MS || 5 * 60 * 1000);
const RESEND_API_KEY = process.env.BLINQ_RESEND_API_KEY || process.env.RESEND_API_KEY || "";
const RESEND_FROM = process.env.BLINQ_RESEND_FROM || "Blinq Messenger <no-reply@mail.blinqm.net>";
const FEEDBACK_TO = process.env.BLINQ_FEEDBACK_TO || "exeinn.info@gmail.com";
const VERBOSE_LOGS = process.env.BLINQ_VERBOSE_LOGS === "1";

fs.mkdirSync(DATA_DIR, { recursive: true });
fs.mkdirSync(UPLOAD_DIR, { recursive: true });

let firebaseMessaging = null;
try {
  if (fs.existsSync(FIREBASE_SERVICE_ACCOUNT_PATH)) {
    const admin = require("firebase-admin");
    const serviceAccount = JSON.parse(fs.readFileSync(FIREBASE_SERVICE_ACCOUNT_PATH, "utf8"));
    admin.initializeApp({
      credential: admin.credential.cert(serviceAccount)
    });
    firebaseMessaging = admin.messaging();
    console.log("Firebase Cloud Messaging enabled.");
  } else {
    console.log(`Firebase service account not found at ${FIREBASE_SERVICE_ACCOUNT_PATH}; push notifications disabled.`);
  }
} catch (error) {
  console.log("Firebase Cloud Messaging disabled:", error.message);
}

function now() {
  return new Date().toISOString();
}

const db = new Database(SQLITE_PATH);
db.pragma("journal_mode = WAL");
db.pragma("foreign_keys = ON");
db.exec(`
  CREATE TABLE IF NOT EXISTS meta (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
  );
  CREATE TABLE IF NOT EXISTS users (
    id TEXT PRIMARY KEY,
    username TEXT NOT NULL UNIQUE,
    display_name TEXT NOT NULL,
    password_salt TEXT NOT NULL,
    password_hash TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'Offline',
    personal_message TEXT NOT NULL DEFAULT '',
    avatar TEXT NOT NULL DEFAULT '',
    theme_color TEXT NOT NULL DEFAULT '',
    searchable INTEGER NOT NULL DEFAULT 1,
    email TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL,
    last_seen_at TEXT NOT NULL
  );
  CREATE TABLE IF NOT EXISTS sessions (
    token TEXT PRIMARY KEY,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at TEXT NOT NULL
  );
  CREATE INDEX IF NOT EXISTS idx_sessions_user_id ON sessions(user_id);
  CREATE TABLE IF NOT EXISTS contacts (
    a TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    b TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at TEXT NOT NULL,
    PRIMARY KEY (a, b)
  );
  CREATE INDEX IF NOT EXISTS idx_contacts_a ON contacts(a);
  CREATE INDEX IF NOT EXISTS idx_contacts_b ON contacts(b);
  CREATE TABLE IF NOT EXISTS contact_requests (
    id TEXT PRIMARY KEY,
    from_user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    to_user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    status TEXT NOT NULL,
    created_at TEXT NOT NULL,
    responded_at TEXT
  );
  CREATE INDEX IF NOT EXISTS idx_contact_requests_to_status ON contact_requests(to_user_id, status);
  CREATE INDEX IF NOT EXISTS idx_contact_requests_from_to_status ON contact_requests(from_user_id, to_user_id, status);
  CREATE TABLE IF NOT EXISTS queued_messages (
    id TEXT PRIMARY KEY,
    from_user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    to_user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    kind TEXT NOT NULL,
    body TEXT NOT NULL DEFAULT '',
    is_html INTEGER NOT NULL DEFAULT 0,
    file_name TEXT NOT NULL DEFAULT '',
    mime_type TEXT NOT NULL DEFAULT '',
    image_id TEXT NOT NULL DEFAULT '',
    size INTEGER NOT NULL DEFAULT 0,
    data TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL
  );
  CREATE INDEX IF NOT EXISTS idx_queued_messages_to_created ON queued_messages(to_user_id, created_at);
  CREATE INDEX IF NOT EXISTS idx_queued_messages_from ON queued_messages(from_user_id);
  CREATE TABLE IF NOT EXISTS push_tokens (
    token TEXT PRIMARY KEY,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    platform TEXT NOT NULL DEFAULT 'android',
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
  );
  CREATE INDEX IF NOT EXISTS idx_push_tokens_user_id ON push_tokens(user_id);
  CREATE TABLE IF NOT EXISTS password_reset_codes (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    code_salt TEXT NOT NULL,
    code_hash TEXT NOT NULL,
    expires_at TEXT NOT NULL,
    attempts INTEGER NOT NULL DEFAULT 0,
    used_at TEXT,
    created_at TEXT NOT NULL
  );
  CREATE INDEX IF NOT EXISTS idx_password_reset_codes_user ON password_reset_codes(user_id, created_at);
`);

const userColumns = db.prepare("PRAGMA table_info(users)").all().map((column) => column.name);
if (!userColumns.includes("searchable")) {
  db.exec("ALTER TABLE users ADD COLUMN searchable INTEGER NOT NULL DEFAULT 1");
}
if (!userColumns.includes("email")) {
  db.exec("ALTER TABLE users ADD COLUMN email TEXT NOT NULL DEFAULT ''");
}
db.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_users_email ON users(email) WHERE email <> ''");

const statements = {
  getMeta: db.prepare("SELECT value FROM meta WHERE key = ?"),
  setMeta: db.prepare("INSERT INTO meta(key, value) VALUES(?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value"),
  userByUsername: db.prepare("SELECT * FROM users WHERE username = ?"),
  userByEmail: db.prepare("SELECT * FROM users WHERE email = ?"),
  userById: db.prepare("SELECT * FROM users WHERE id = ?"),
  userCount: db.prepare("SELECT COUNT(*) AS count FROM users"),
  insertUser: db.prepare(`
    INSERT INTO users(id, username, display_name, password_salt, password_hash, status, personal_message, avatar, theme_color, searchable, email, created_at, last_seen_at)
    VALUES(@id, @username, @displayName, @passwordSalt, @passwordHash, @status, @personalMessage, @avatar, @themeColor, @searchable, @email, @createdAt, @lastSeenAt)
  `),
  updateUserPresence: db.prepare(`
    UPDATE users
    SET status = @status,
        personal_message = @personalMessage,
        display_name = @displayName,
        avatar = @avatar,
        theme_color = @themeColor,
        searchable = @searchable,
        last_seen_at = @lastSeenAt
    WHERE id = @id
  `),
  updateUserPassword: db.prepare("UPDATE users SET password_salt = ?, password_hash = ? WHERE id = ?"),
  updateUserEmail: db.prepare("UPDATE users SET email = ? WHERE id = ?"),
  updateUserOnline: db.prepare("UPDATE users SET status = ?, last_seen_at = ? WHERE id = ?"),
  updateAllUsersOffline: db.prepare("UPDATE users SET status = 'Offline', last_seen_at = ? WHERE status <> 'Offline'"),
  deleteUser: db.prepare("DELETE FROM users WHERE id = ?"),
  insertSession: db.prepare("INSERT INTO sessions(token, user_id, created_at) VALUES(?, ?, ?)"),
  sessionByToken: db.prepare("SELECT * FROM sessions WHERE token = ?"),
  deleteSession: db.prepare("DELETE FROM sessions WHERE token = ?"),
  deleteSessionsForUser: db.prepare("DELETE FROM sessions WHERE user_id = ?"),
  contactPair: db.prepare("SELECT 1 FROM contacts WHERE (a = ? AND b = ?) OR (a = ? AND b = ?) LIMIT 1"),
  contactIds: db.prepare(`
    SELECT CASE WHEN a = ? THEN b ELSE a END AS id
    FROM contacts
    WHERE a = ? OR b = ?
  `),
  insertContact: db.prepare("INSERT OR IGNORE INTO contacts(a, b, created_at) VALUES(?, ?, ?)"),
  deleteContactPair: db.prepare("DELETE FROM contacts WHERE (a = ? AND b = ?) OR (a = ? AND b = ?)"),
  deleteContactsForUser: db.prepare("DELETE FROM contacts WHERE a = ? OR b = ?"),
  pendingRequestsForUser: db.prepare("SELECT * FROM contact_requests WHERE to_user_id = ? AND status = 'pending'"),
  pendingRequestFromTo: db.prepare("SELECT * FROM contact_requests WHERE from_user_id = ? AND to_user_id = ? AND status = 'pending'"),
  requestByIdForRecipient: db.prepare("SELECT * FROM contact_requests WHERE id = ? AND to_user_id = ? AND status = 'pending'"),
  insertRequest: db.prepare("INSERT INTO contact_requests(id, from_user_id, to_user_id, status, created_at) VALUES(?, ?, ?, 'pending', ?)"),
  updateRequest: db.prepare("UPDATE contact_requests SET status = ?, responded_at = ? WHERE id = ?"),
  deleteRequestsForUser: db.prepare("DELETE FROM contact_requests WHERE from_user_id = ? OR to_user_id = ?"),
  insertQueuedMessage: db.prepare(`
    INSERT INTO queued_messages(id, from_user_id, to_user_id, kind, body, is_html, file_name, mime_type, image_id, size, data, created_at)
    VALUES(@id, @from, @to, @kind, @body, @isHtml, @fileName, @mimeType, @imageId, @size, @data, @createdAt)
  `),
  queuedMessagesForUser: db.prepare("SELECT * FROM queued_messages WHERE to_user_id = ? ORDER BY created_at, id"),
  queuedMessageStatsForUser: db.prepare("SELECT COUNT(*) AS count, COALESCE(SUM(LENGTH(body)), 0) AS bytes FROM queued_messages WHERE to_user_id = ?"),
  deleteQueuedMessage: db.prepare("DELETE FROM queued_messages WHERE id = ?"),
  deleteExpiredQueuedMessages: db.prepare("DELETE FROM queued_messages WHERE created_at < ?"),
  deleteQueuedMessagesForUser: db.prepare("DELETE FROM queued_messages WHERE from_user_id = ? OR to_user_id = ?"),
  upsertPushToken: db.prepare(`
    INSERT INTO push_tokens(token, user_id, platform, created_at, updated_at)
    VALUES(@token, @userId, @platform, @createdAt, @updatedAt)
    ON CONFLICT(token) DO UPDATE SET
      user_id = excluded.user_id,
      platform = excluded.platform,
      updated_at = excluded.updated_at
  `),
  pushTokensForUser: db.prepare("SELECT token FROM push_tokens WHERE user_id = ? AND platform = 'android'"),
  deletePushToken: db.prepare("DELETE FROM push_tokens WHERE token = ?"),
  deletePushTokensForUser: db.prepare("DELETE FROM push_tokens WHERE user_id = ?"),
  insertPasswordResetCode: db.prepare(`
    INSERT INTO password_reset_codes(id, user_id, code_salt, code_hash, expires_at, attempts, used_at, created_at)
    VALUES(@id, @userId, @codeSalt, @codeHash, @expiresAt, 0, NULL, @createdAt)
  `),
  latestPasswordResetCode: db.prepare(`
    SELECT *
    FROM password_reset_codes
    WHERE user_id = ? AND used_at IS NULL
    ORDER BY created_at DESC
    LIMIT 1
  `),
  incrementPasswordResetAttempts: db.prepare("UPDATE password_reset_codes SET attempts = attempts + 1 WHERE id = ?"),
  markPasswordResetUsed: db.prepare("UPDATE password_reset_codes SET used_at = ? WHERE id = ?"),
  deletePasswordResetCodesForUser: db.prepare("DELETE FROM password_reset_codes WHERE user_id = ?"),
  deleteExpiredPasswordResetCodes: db.prepare("DELETE FROM password_reset_codes WHERE expires_at < ? OR used_at IS NOT NULL"),
  searchUsers: db.prepare(`
    SELECT *
    FROM users
    WHERE searchable = 1
      AND id <> @userId
      AND (LOWER(username) LIKE @prefix OR LOWER(display_name) LIKE @contains OR LOWER(username || '@${DOMAIN}') LIKE @prefix)
    ORDER BY CASE WHEN LOWER(username) LIKE @prefix OR LOWER(username || '@${DOMAIN}') LIKE @prefix THEN 0 ELSE 1 END, username
    LIMIT @limit
  `),
};

function rowToUser(row) {
  if (!row) return null;
  return {
    id: row.id,
    username: row.username,
    displayName: row.display_name,
    passwordSalt: row.password_salt,
    passwordHash: row.password_hash,
    status: row.status,
    personalMessage: row.personal_message,
    avatar: row.avatar,
    themeColor: row.theme_color,
    searchable: row.searchable !== 0,
    email: row.email || "",
    createdAt: row.created_at,
    lastSeenAt: row.last_seen_at
  };
}

function requestFromRow(row) {
  if (!row) return null;
  return {
    id: row.id,
    from: row.from_user_id,
    to: row.to_user_id,
    status: row.status,
    createdAt: row.created_at,
    respondedAt: row.responded_at || ""
  };
}

function queuedMessageFromRow(row) {
  if (!row) return null;
  return {
    id: row.id,
    kind: row.kind,
    from: row.from_user_id,
    to: row.to_user_id,
    body: row.body || "",
    isHtml: row.is_html !== 0,
    fileName: row.file_name || "",
    mimeType: row.mime_type || "",
    imageId: row.image_id || "",
    size: Number(row.size || 0),
    data: row.data || "",
    createdAt: row.created_at
  };
}

function nextUserIdFromLegacy(legacyDb) {
  const configured = Number(legacyDb && legacyDb.nextUserId);
  if (Number.isInteger(configured) && configured > 0) return configured;
  let next = 1;
  for (const user of legacyDb && Array.isArray(legacyDb.users) ? legacyDb.users : []) {
    const match = String(user.id || "").match(/^u(\d+)$/);
    if (match) next = Math.max(next, Number(match[1]) + 1);
  }
  return next;
}

function migrateLegacyJsonIfNeeded() {
  if (!fs.existsSync(LEGACY_JSON_PATH)) return;
  if (statements.userCount.get().count > 0) return;

  let legacyDb;
  try {
    legacyDb = JSON.parse(fs.readFileSync(LEGACY_JSON_PATH, "utf8"));
  } catch (error) {
    console.log("Could not read legacy db.json:", error.message);
    return;
  }

  const migrate = db.transaction(() => {
    for (const user of Array.isArray(legacyDb.users) ? legacyDb.users : []) {
      if (!user.id || !user.username || !user.passwordSalt || !user.passwordHash) continue;
      statements.insertUser.run({
        id: String(user.id),
        username: String(user.username),
        displayName: String(user.displayName || user.username),
        passwordSalt: String(user.passwordSalt),
        passwordHash: String(user.passwordHash),
        status: String(user.status || "Offline"),
        personalMessage: String(user.personalMessage || ""),
        avatar: String(user.avatar || ""),
        themeColor: String(user.themeColor || ""),
        searchable: user.searchable === false ? 0 : 1,
        email: String(user.email || "").trim().toLowerCase(),
        createdAt: String(user.createdAt || now()),
        lastSeenAt: String(user.lastSeenAt || now())
      });
    }
    for (const session of Array.isArray(legacyDb.sessions) ? legacyDb.sessions : []) {
      if (session.token && session.userId && statements.userById.get(session.userId)) {
        statements.insertSession.run(String(session.token), String(session.userId), String(session.createdAt || now()));
      }
    }
    for (const contact of Array.isArray(legacyDb.contacts) ? legacyDb.contacts : []) {
      if (contact.a && contact.b && statements.userById.get(contact.a) && statements.userById.get(contact.b)) {
        statements.insertContact.run(String(contact.a), String(contact.b), String(contact.createdAt || now()));
      }
    }
    for (const request of Array.isArray(legacyDb.contactRequests) ? legacyDb.contactRequests : []) {
      if (!request.id || !request.from || !request.to) continue;
      if (!statements.userById.get(request.from) || !statements.userById.get(request.to)) continue;
      statements.insertRequest.run(String(request.id), String(request.from), String(request.to), String(request.createdAt || now()));
      if (request.status && request.status !== "pending") {
        statements.updateRequest.run(String(request.status), String(request.respondedAt || now()), String(request.id));
      }
    }
    statements.setMeta.run("next_user_id", String(nextUserIdFromLegacy(legacyDb)));
  });

  migrate();
  console.log("Migrated legacy db.json to SQLite. Keeping db.json as a backup.");
}

migrateLegacyJsonIfNeeded();

const clientsByUserId = new Map();
const rateBuckets = new Map();

statements.updateAllUsersOffline.run(now());

function deleteUploadFile(fileName) {
  const safeName = path.basename(String(fileName || ""));
  if (!safeName) return;
  fs.unlink(path.join(UPLOAD_DIR, safeName), () => {});
}

function cleanupOldUploads(maxAgeMs = UPLOAD_TTL_MS) {
  const cutoff = Date.now() - Math.max(0, maxAgeMs);
  fs.readdir(UPLOAD_DIR, { withFileTypes: true }, (error, entries) => {
    if (error) return;
    for (const entry of entries) {
      if (!entry.isFile()) continue;
      const filePath = path.join(UPLOAD_DIR, entry.name);
      fs.stat(filePath, (statError, stats) => {
        if (!statError && stats.mtimeMs <= cutoff) {
          deleteUploadFile(entry.name);
        }
      });
    }
  });
}

function scheduleUploadDeletion(fileName) {
  const timer = setTimeout(() => deleteUploadFile(fileName), Math.max(1000, UPLOAD_TTL_MS));
  if (typeof timer.unref === "function") timer.unref();
}

function normalizeUsername(value) {
  let text = String(value || "").trim().toLowerCase();
  if (text.endsWith(`@${DOMAIN}`)) text = text.slice(0, -DOMAIN.length - 1);
  return text;
}

function normalizeEmail(value) {
  return String(value || "").trim().toLowerCase().slice(0, 254);
}

function isValidEmail(value) {
  return /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(value) && value.length <= 254;
}

function normalizeSearchQuery(value) {
  return String(value || "").trim().replace(/\s+/g, " ").slice(0, 60);
}

function remoteAddressFor(socket) {
  return String(socket.remoteAddress || "unknown").replace(/^::ffff:/, "");
}

function isRateLimited(key, limit, windowMs) {
  const cutoff = Date.now() - windowMs;
  const bucket = (rateBuckets.get(key) || []).filter((timestamp) => timestamp >= cutoff);
  if (bucket.length >= limit) {
    rateBuckets.set(key, bucket);
    return true;
  }
  bucket.push(Date.now());
  rateBuckets.set(key, bucket);
  return false;
}

function isSearchRateLimited(socket) {
  return isRateLimited(`search:${socket.userId || remoteAddressFor(socket)}`, 10, 60 * 1000);
}

function blinqId(username) {
  return `${username}@${DOMAIN}`;
}

function normalizeStatus(value) {
  const status = String(value || "").trim().replace(/\s+/g, " ").slice(0, 40);
  if (!status || status.toLowerCase() === "offline") return "Available";
  return status;
}

function publicUser(user) {
  return {
    id: user.id,
    username: user.username,
    blinqId: blinqId(user.username),
    displayName: user.displayName,
    status: user.status || "Offline",
    personalMessage: user.personalMessage || "",
    avatar: user.avatar || "",
    themeColor: user.themeColor || "",
    searchable: user.searchable !== false,
    lastSeenAt: user.lastSeenAt || ""
  };
}

function send(socket, object) {
  socket.write(JSON.stringify(object) + "\n");
}

function sendError(socket, code, message) {
  send(socket, { type: "error", code, message });
}

function verboseLog(...args) {
  if (VERBOSE_LOGS) {
    console.log(...args);
  }
}

function isRoutineSocketError(error) {
  const code = String(error && error.code || "");
  return code === "ECONNRESET" || code === "ETIMEDOUT" || code === "EPIPE";
}

function hashPassword(password, salt = crypto.randomBytes(16).toString("hex")) {
  const hash = crypto.pbkdf2Sync(String(password), salt, 120000, 32, "sha256").toString("hex");
  return { salt, hash };
}

function verifyPassword(password, user) {
  const result = hashPassword(password, user.passwordSalt);
  return crypto.timingSafeEqual(Buffer.from(result.hash, "hex"), Buffer.from(user.passwordHash, "hex"));
}

function sendEmailWithResend(to, subject, text, html) {
  if (!RESEND_API_KEY) {
    console.log("Email not sent: BLINQ_RESEND_API_KEY is not configured.");
    return Promise.resolve(false);
  }

  const payload = JSON.stringify({
    from: RESEND_FROM,
    to: [to],
    subject,
    text,
    html
  });

  return new Promise((resolve) => {
    const request = https.request({
      hostname: "api.resend.com",
      path: "/emails",
      method: "POST",
      headers: {
        Authorization: `Bearer ${RESEND_API_KEY}`,
        "Content-Type": "application/json",
        "Content-Length": Buffer.byteLength(payload)
      },
      timeout: 10000
    }, (response) => {
      let body = "";
      response.setEncoding("utf8");
      response.on("data", (chunk) => {
        body += chunk;
      });
      response.on("end", () => {
        if (response.statusCode >= 200 && response.statusCode < 300) {
          resolve(true);
        } else {
          console.log(`Resend email failed (${response.statusCode}): ${body.slice(0, 300)}`);
          resolve(false);
        }
      });
    });
    request.on("timeout", () => {
      request.destroy(new Error("Resend request timed out"));
    });
    request.on("error", (error) => {
      console.log("Resend email error:", error.message);
      resolve(false);
    });
    request.write(payload);
    request.end();
  });
}

function sendPasswordResetEmail(user, code) {
  if (!user || !user.email) return;
  const subject = "Your Blinq Messenger password reset code";
  const text = [
    "Your Blinq Messenger password reset code is:",
    "",
    code,
    "",
    "This code expires in 5 minutes. If you did not request it, you can ignore this email."
  ].join("\n");
  const html = `<p>Your Blinq Messenger password reset code is:</p><p style="font-size:24px;font-weight:700;letter-spacing:4px">${code}</p><p>This code expires in 5 minutes. If you did not request it, you can ignore this email.</p>`;
  sendEmailWithResend(user.email, subject, text, html).then((sent) => {
    if (sent) console.log(`Password reset email sent for ${user.username}.`);
  });
}

function escapeHtml(value) {
  return String(value || "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

function sendFeedbackEmail(user, feedback) {
  const category = String(feedback.category || "Other").trim().slice(0, 40) || "Other";
  const message = String(feedback.message || "").trim().slice(0, 4000);
  const contactEmail = normalizeEmail(feedback.contactEmail);
  const platform = String(feedback.platform || "Unknown").trim().slice(0, 60) || "Unknown";
  const appVersion = String(feedback.appVersion || "").trim().slice(0, 40) || "unknown";
  const debugInfo = String(feedback.debugInfo || "").trim().slice(0, 6000);
  const subject = `[Blinq Feedback] ${category} from ${user.username}`;
  const text = [
    `Type: ${category}`,
    `From: ${user.displayName} <${blinqId(user.username)}>`,
    `Account ID: ${user.id}`,
    `Contact email: ${contactEmail || "not provided"}`,
    `Platform: ${platform}`,
    `App version: ${appVersion}`,
    `Received: ${now()}`,
    "",
    "Message:",
    message,
    "",
    "Debug info:",
    debugInfo || "not included"
  ].join("\n");
  const html = [
    `<h2>Blinq Messenger Feedback</h2>`,
    `<p><b>Type:</b> ${escapeHtml(category)}</p>`,
    `<p><b>From:</b> ${escapeHtml(user.displayName)} &lt;${escapeHtml(blinqId(user.username))}&gt;</p>`,
    `<p><b>Account ID:</b> ${escapeHtml(user.id)}</p>`,
    `<p><b>Contact email:</b> ${escapeHtml(contactEmail || "not provided")}</p>`,
    `<p><b>Platform:</b> ${escapeHtml(platform)}</p>`,
    `<p><b>App version:</b> ${escapeHtml(appVersion)}</p>`,
    `<p><b>Received:</b> ${escapeHtml(now())}</p>`,
    `<h3>Message</h3>`,
    `<pre style="white-space:pre-wrap;font-family:system-ui,Segoe UI,sans-serif">${escapeHtml(message)}</pre>`,
    `<h3>Debug info</h3>`,
    `<pre style="white-space:pre-wrap;font-family:Consolas,monospace">${escapeHtml(debugInfo || "not included")}</pre>`
  ].join("");
  return sendEmailWithResend(FEEDBACK_TO, subject, text, html);
}

function newId(prefix) {
  return `${prefix}_${crypto.randomUUID()}`;
}

function nextUserId() {
  const current = Number(statements.getMeta.get("next_user_id")?.value || "1");
  const safeCurrent = Number.isInteger(current) && current > 0 ? current : 1;
  statements.setMeta.run("next_user_id", String(safeCurrent + 1));
  return `u${safeCurrent}`;
}

function findUserByUsername(username) {
  return rowToUser(statements.userByUsername.get(username));
}

function findUserByEmail(email) {
  return rowToUser(statements.userByEmail.get(email));
}

function findUserByIdentifier(identifier) {
  const value = String(identifier || "").trim().toLowerCase();
  if (!value) return null;
  if (value.includes("@") && !value.endsWith(`@${DOMAIN}`)) {
    return findUserByEmail(normalizeEmail(value));
  }
  return findUserByUsername(normalizeUsername(value));
}

function findUserById(id) {
  return rowToUser(statements.userById.get(id));
}

function areContacts(a, b) {
  return !!statements.contactPair.get(a, b, b, a);
}

function contactUsersFor(userId) {
  return statements.contactIds.all(userId, userId, userId)
    .map((row) => findUserById(row.id))
    .filter(Boolean);
}

function pendingRequestsFor(userId) {
  return statements.pendingRequestsForUser.all(userId)
    .map(requestFromRow)
    .filter(Boolean)
    .map((request) => ({ ...request, fromUser: publicUser(findUserById(request.from)) }))
    .filter((request) => request.fromUser);
}

function onlineSocketFor(userId) {
  return clientsByUserId.get(userId) || null;
}

function effectivePublicUser(user) {
  const result = publicUser(user);
  if (result.status === "Invisible") result.status = "Offline";
  return result;
}

function broadcastEffectivePresence(user) {
  for (const contact of contactUsersFor(user.id)) {
    const socket = onlineSocketFor(contact.id);
    if (socket) send(socket, { type: "presence", user: effectivePublicUser(user) });
  }
}

function sessionPayload(user) {
  return {
    user: publicUser(user),
    contacts: contactUsersFor(user.id).map(effectivePublicUser),
    contactRequests: pendingRequestsFor(user.id)
  };
}

function authenticateSocket(socket, user, token) {
  if (socket.userId && clientsByUserId.get(socket.userId) === socket) {
    clientsByUserId.delete(socket.userId);
  }
  socket.userId = user.id;
  socket.sessionToken = token;
  const status = user.status === "Invisible" ? "Invisible" : "Available";
  statements.updateUserOnline.run(status, now(), user.id);
  const updatedUser = findUserById(user.id);
  clientsByUserId.set(user.id, socket);
  send(socket, { type: "authenticated", token, ...sessionPayload(updatedUser) });
  broadcastEffectivePresence(updatedUser);
  deliverQueuedMessages(user.id);
}

function createToken(userId) {
  const token = crypto.randomBytes(32).toString("base64url");
  statements.insertSession.run(token, userId, now());
  return token;
}

function requireAuth(socket) {
  if (!socket.userId) {
    sendError(socket, "auth_required", "Sign in first.");
    return null;
  }
  return findUserById(socket.userId);
}

function sendContacts(socket, user) {
  if (!socket || !user) return;
  send(socket, {
    type: "contacts",
    contacts: contactUsersFor(user.id).map(effectivePublicUser),
    contactRequests: pendingRequestsFor(user.id)
  });
}

function extensionForMime(mime) {
  const normalized = String(mime || "").toLowerCase();
  if (normalized === "image/png") return ".png";
  if (normalized === "image/jpeg" || normalized === "image/jpg") return ".jpg";
  if (normalized === "image/gif") return ".gif";
  if (normalized === "image/webp") return ".webp";
  if (normalized === "image/bmp") return ".bmp";
  return "";
}

function cleanupExpiredQueuedMessages() {
  const cutoff = new Date(Date.now() - Math.max(0, QUEUED_MESSAGE_TTL_MS)).toISOString();
  statements.deleteExpiredQueuedMessages.run(cutoff);
}

function storeQueuedMessage(message) {
  cleanupExpiredQueuedMessages();
  if (message.kind !== "message") {
    return { ok: false, code: "offline_image", message: "Images can only be sent when the contact is online." };
  }

  const stats = statements.queuedMessageStatsForUser.get(message.to);
  const bodyBytes = Buffer.byteLength(String(message.body || ""), "utf8");
  if (Number(stats.count || 0) >= MAX_QUEUED_MESSAGES_PER_USER) {
    return { ok: false, code: "queue_full", message: "That contact has too many queued messages. Please try again later." };
  }
  if (Number(stats.bytes || 0) + bodyBytes > MAX_QUEUED_TEXT_BYTES_PER_USER) {
    return { ok: false, code: "queue_full", message: "That contact's queued messages are full. Please try again later." };
  }

  statements.insertQueuedMessage.run({
    id: message.id,
    from: message.from,
    to: message.to,
    kind: message.kind,
    body: message.body || "",
    isHtml: message.isHtml ? 1 : 0,
    fileName: message.fileName || "",
    mimeType: message.mimeType || "",
    imageId: message.imageId || "",
    size: Number(message.size || 0),
    data: message.data || "",
    createdAt: message.createdAt || now()
  });
  return { ok: true };
}

function sendReceiptToSender(message, toUser, status) {
  const fromSocket = onlineSocketFor(message.from);
  if (!fromSocket || !toUser) return;
  send(fromSocket, {
    type: "receipt",
    from: blinqId(toUser.username),
    fromUser: publicUser(toUser),
    messageId: message.id,
    status
  });
}

function pushBodyForMessage(message, fromUser) {
  if (message.kind === "imageMessage") return "sent you an image";
  if (message.kind === "buzz") return "whistled at you";
  if (message.isHtml) return "sent you a rich message";
  return "sent you a message";
}

function sendPushToUser(toUserId, fromUser, message) {
  if (!firebaseMessaging || !toUserId || !fromUser || !message) return;
  const tokens = statements.pushTokensForUser.all(toUserId).map((row) => row.token).filter(Boolean);
  if (!tokens.length) return;
  const title = String(fromUser.displayName || blinqId(fromUser.username));
  const body = pushBodyForMessage(message, fromUser);

  const payload = {
    tokens,
    notification: {
      title,
      body
    },
    data: {
      type: String(message.kind || "message"),
      peerId: String(fromUser.id),
      senderName: title,
      messageId: String(message.id || ""),
      title,
      body
    },
    android: {
      priority: "high",
      notification: {
        channelId: "blinq_messages",
        clickAction: "com.ei.blinqmessenger.OPEN_BLINQ_CHAT",
        title,
        body
      }
    }
  };

  firebaseMessaging.sendEachForMulticast(payload)
    .then((result) => {
      const successCount = result.responses.filter((response) => response.success).length;
      if (successCount > 0) {
        verboseLog(`Push notification sent to ${successCount}/${tokens.length} device(s) for user ${toUserId}.`);
      }
      result.responses.forEach((response, index) => {
        if (response.success) return;
        const code = response.error && response.error.code;
        if (code === "messaging/registration-token-not-registered" || code === "messaging/invalid-registration-token") {
          statements.deletePushToken.run(tokens[index]);
        }
        console.log("Push notification failed:", code || (response.error && response.error.message) || "unknown error");
      });
    })
    .catch((error) => {
      console.log("Push notification error:", error.message);
    });
}

function deliverMessage(message) {
  const from = findUserById(message.from);
  const to = findUserById(message.to);
  if (!from || !to) return false;

  const toSocket = onlineSocketFor(message.to);
  if (toSocket) {
    send(toSocket, { type: message.kind, message, fromUser: publicUser(from) });
    sendReceiptToSender(message, to, "Delivered");
    return "delivered";
  }
  const queued = storeQueuedMessage(message);
  if (queued.ok) {
    sendReceiptToSender(message, to, "Queued");
    sendPushToUser(message.to, from, message);
    return "queued";
  }
  const fromSocket = onlineSocketFor(message.from);
  if (fromSocket) {
    sendError(fromSocket, queued.code, queued.message);
  }
  return "failed";
}

function deliverQueuedMessages(userId) {
  const socket = onlineSocketFor(userId);
  if (!socket) return;
  for (const row of statements.queuedMessagesForUser.all(userId)) {
    const message = queuedMessageFromRow(row);
    const from = message && findUserById(message.from);
    const to = message && findUserById(message.to);
    if (!message || !from || !to || !areContacts(message.from, message.to)) {
      statements.deleteQueuedMessage.run(row.id);
      continue;
    }
    send(socket, { type: message.kind, message, fromUser: publicUser(from) });
    statements.deleteQueuedMessage.run(message.id);
    sendReceiptToSender(message, to, "Delivered");
  }
}

function handleMessage(socket, msg) {
  if (!msg || typeof msg !== "object") return;
  const type = String(msg.type || "");

  if (type === "pong") {
    socket.lastPongAt = Date.now();
    return;
  }

  if (type === "signup") {
    const remoteAddress = remoteAddressFor(socket);
    if (isRateLimited(`signup:${remoteAddress}`, 5, 60 * 60 * 1000)) {
      return sendError(socket, "rate_limited", "Too many account attempts. Please wait before trying again.");
    }
    const username = normalizeUsername(msg.username);
    const password = String(msg.password || "");
    const displayName = String(msg.displayName || username).trim().slice(0, 60);
    const email = normalizeEmail(msg.email);
    if (!/^[a-z0-9._]{3,32}$/.test(username)) return sendError(socket, "bad_username", "Use 3-32 lowercase letters, numbers, dots, or underscores.");
    if (password.length < 8) return sendError(socket, "bad_password", "Password must be at least 8 characters.");
    if (email && !isValidEmail(email)) return sendError(socket, "bad_email", "Enter a valid recovery email address.");
    if (email && findUserByEmail(email)) return sendError(socket, "email_taken", "That recovery email is already used by another account.");
    if (findUserByUsername(username)) return sendError(socket, "username_taken", "That Blinq ID is already taken.");
    const passwordData = hashPassword(password);
    const user = {
      id: nextUserId(),
      username,
      displayName: displayName || username,
      passwordSalt: passwordData.salt,
      passwordHash: passwordData.hash,
      status: "Available",
      personalMessage: "",
      avatar: "",
      themeColor: "",
      searchable: 1,
      email,
      createdAt: now(),
      lastSeenAt: now()
    };
    statements.insertUser.run(user);
    const token = createToken(user.id);
    return authenticateSocket(socket, findUserById(user.id), token);
  }

  if (type === "requestPasswordReset") {
    const remoteAddress = remoteAddressFor(socket);
    if (isRateLimited(`reset-ip:${remoteAddress}`, 5, 60 * 60 * 1000)) {
      return send(socket, { type: "passwordResetRequested" });
    }
    const identifier = String(msg.identifier || msg.username || msg.email || "").trim();
    if (identifier.length < 3 || identifier.length > 254) {
      return send(socket, { type: "passwordResetRequested" });
    }
    if (isRateLimited(`reset-id:${identifier.toLowerCase()}`, 3, 15 * 60 * 1000)) {
      return send(socket, { type: "passwordResetRequested" });
    }

    const user = findUserByIdentifier(identifier);
    if (user && user.email && isValidEmail(user.email)) {
      statements.deleteExpiredPasswordResetCodes.run(now());
      const code = String(crypto.randomInt(0, 1000000)).padStart(6, "0");
      const codeData = hashPassword(code);
      statements.insertPasswordResetCode.run({
        id: newId("pr"),
        userId: user.id,
        codeSalt: codeData.salt,
        codeHash: codeData.hash,
        expiresAt: new Date(Date.now() + PASSWORD_RESET_CODE_TTL_MS).toISOString(),
        createdAt: now()
      });
      sendPasswordResetEmail(user, code);
    }
    return send(socket, { type: "passwordResetRequested" });
  }

  if (type === "resetPassword") {
    const remoteAddress = remoteAddressFor(socket);
    if (isRateLimited(`reset-finish-ip:${remoteAddress}`, 15, 60 * 60 * 1000)) {
      return sendError(socket, "rate_limited", "Too many reset attempts. Please wait before trying again.");
    }
    const identifier = String(msg.identifier || msg.username || msg.email || "").trim();
    const code = String(msg.code || "").trim().replace(/\s+/g, "");
    const newPassword = String(msg.newPassword || "");
    if (!/^\d{6}$/.test(code)) return sendError(socket, "bad_reset_code", "Enter the 6-digit reset code.");
    if (newPassword.length < 8) return sendError(socket, "bad_new_password", "New password must be at least 8 characters.");
    const user = findUserByIdentifier(identifier);
    const reset = user && statements.latestPasswordResetCode.get(user.id);
    if (!user || !reset || reset.used_at || new Date(reset.expires_at).getTime() < Date.now()) {
      return sendError(socket, "bad_reset_code", "Reset code is invalid or expired.");
    }
    if (Number(reset.attempts || 0) >= 5) {
      statements.markPasswordResetUsed.run(now(), reset.id);
      return sendError(socket, "bad_reset_code", "Reset code is invalid or expired.");
    }
    const codeResult = hashPassword(code, reset.code_salt);
    const matches = crypto.timingSafeEqual(Buffer.from(codeResult.hash, "hex"), Buffer.from(reset.code_hash, "hex"));
    if (!matches) {
      statements.incrementPasswordResetAttempts.run(reset.id);
      return sendError(socket, "bad_reset_code", "Reset code is invalid or expired.");
    }
    const passwordData = hashPassword(newPassword);
    db.transaction(() => {
      statements.updateUserPassword.run(passwordData.salt, passwordData.hash, user.id);
      statements.deleteSessionsForUser.run(user.id);
      statements.markPasswordResetUsed.run(now(), reset.id);
      statements.deletePasswordResetCodesForUser.run(user.id);
    })();
    return send(socket, { type: "passwordReset" });
  }

  if (type === "login") {
    const remoteAddress = remoteAddressFor(socket);
    const username = normalizeUsername(msg.username);
    if (isRateLimited(`login-ip:${remoteAddress}`, 20, 10 * 60 * 1000)
        || isRateLimited(`login-user:${username}:${remoteAddress}`, 8, 10 * 60 * 1000)) {
      return sendError(socket, "rate_limited", "Too many sign-in attempts. Please wait before trying again.");
    }
    const user = findUserByUsername(username);
    if (!user || !verifyPassword(msg.password || "", user)) return sendError(socket, "bad_login", "Blinq ID or password is incorrect.");
    const token = createToken(user.id);
    return authenticateSocket(socket, user, token);
  }

  if (type === "resume") {
    if (isRateLimited(`resume:${remoteAddressFor(socket)}`, 60, 10 * 60 * 1000)) {
      return sendError(socket, "rate_limited", "Too many session attempts. Please wait before trying again.");
    }
    const token = String(msg.token || "");
    const session = statements.sessionByToken.get(token);
    const user = session && findUserById(session.user_id);
    if (!user) return sendError(socket, "bad_session", "Saved sign-in expired.");
    return authenticateSocket(socket, user, token);
  }

  const user = requireAuth(socket);
  if (!user) return;

  if (type === "setPresence") {
    const updated = {
      id: user.id,
      status: normalizeStatus(msg.status),
      personalMessage: String(msg.personalMessage || "").slice(0, 140),
      displayName: user.displayName,
      avatar: user.avatar,
      themeColor: user.themeColor,
      searchable: user.searchable === false ? 0 : 1,
      lastSeenAt: now()
    };
    if (typeof msg.displayName === "string") {
      const displayName = msg.displayName.trim().slice(0, 60);
      if (displayName) updated.displayName = displayName;
    }
    if (typeof msg.avatar === "string") {
      updated.avatar = msg.avatar.slice(0, 300000);
    }
    if (typeof msg.themeColor === "string") {
      const themeColor = msg.themeColor.trim().slice(0, 24);
      if (/^#[0-9a-fA-F]{6}$/.test(themeColor)) updated.themeColor = themeColor;
    }
    if (typeof msg.searchable === "boolean") {
      updated.searchable = msg.searchable ? 1 : 0;
    }
    statements.updateUserPresence.run(updated);
    const savedUser = findUserById(user.id);
    send(socket, { type: "presenceSet", user: publicUser(savedUser) });
    return broadcastEffectivePresence(savedUser);
  }

  if (type === "registerPushToken") {
    const token = String(msg.token || "").trim();
    const platform = String(msg.platform || "android").trim().toLowerCase().slice(0, 20) || "android";
    if (platform !== "android") return sendError(socket, "bad_platform", "Unsupported push platform.");
    if (token.length < 20 || token.length > 4096) return sendError(socket, "bad_push_token", "Invalid push token.");
    const timestamp = now();
    statements.upsertPushToken.run({
      token,
      userId: user.id,
      platform,
      createdAt: timestamp,
      updatedAt: timestamp
    });
    verboseLog(`Registered ${platform} push token for user ${user.username}.`);
    return send(socket, { type: "pushTokenRegistered" });
  }

  if (type === "sendFeedback") {
    if (isRateLimited(`feedback:${user.id}`, 3, 60 * 60 * 1000)) {
      return sendError(socket, "rate_limited", "Please wait before sending more feedback.");
    }
    if (isRateLimited(`feedback-ip:${remoteAddressFor(socket)}`, 12, 60 * 60 * 1000)) {
      return sendError(socket, "rate_limited", "Please wait before sending more feedback.");
    }
    const category = String(msg.category || "Other").trim().slice(0, 40) || "Other";
    const message = String(msg.message || "").trim();
    const contactEmail = normalizeEmail(msg.contactEmail);
    if (message.length < 10) return sendError(socket, "bad_feedback", "Please enter a little more detail.");
    if (message.length > 4000) return sendError(socket, "bad_feedback", "Feedback must be 4000 characters or less.");
    if (contactEmail && !isValidEmail(contactEmail)) return sendError(socket, "bad_email", "Enter a valid contact email address.");

    sendFeedbackEmail(user, {
      category,
      message,
      contactEmail,
      platform: msg.platform,
      appVersion: msg.appVersion,
      debugInfo: msg.debugInfo
    }).then((sent) => {
      if (sent) {
        console.log(`Feedback email sent for ${user.username}.`);
        send(socket, { type: "feedbackSent" });
      } else {
        sendError(socket, "feedback_failed", "Feedback could not be sent right now. Please try again later.");
      }
    });
    return;
  }

  if (type === "findUser") {
    const target = findUserByUsername(normalizeUsername(msg.blinqId || msg.username || msg.to));
    return send(socket, { type: "findUserResult", user: target ? publicUser(target) : null });
  }

  if (type === "searchUsers") {
    if (isSearchRateLimited(socket)) {
      return sendError(socket, "rate_limited", "Please wait a moment before searching again.");
    }
    const query = normalizeSearchQuery(msg.query);
    if (query.length < 2) {
      return send(socket, { type: "userSearchResults", query, users: [] });
    }

    const usernameQuery = normalizeUsername(query);
    const prefix = `${usernameQuery || query.toLowerCase()}%`;
    const contains = `%${query.toLowerCase()}%`;
    const users = statements.searchUsers.all({
      userId: user.id,
      prefix,
      contains,
      limit: 10
    })
      .map(rowToUser)
      .filter((candidate) => candidate && !areContacts(user.id, candidate.id))
      .slice(0, 10)
      .map(publicUser);
    verboseLog(`searchUsers user=${user.username} query="${query}" results=${users.length}`);
    return send(socket, { type: "userSearchResults", query, users });
  }

  if (type === "addContact") {
    if (isRateLimited(`add-contact:${user.id}`, 30, 60 * 1000)) {
      return sendError(socket, "rate_limited", "Please wait a moment before adding more contacts.");
    }
    const target = findUserByUsername(normalizeUsername(msg.blinqId || msg.username || msg.to));
    if (!target) return sendError(socket, "not_found", "That Blinq ID does not exist.");
    if (target.id === user.id) return sendError(socket, "self_contact", "You cannot add yourself.");
    if (areContacts(user.id, target.id)) return sendError(socket, "already_contacts", "That user is already in your contacts.");
    let request = requestFromRow(statements.pendingRequestFromTo.get(user.id, target.id));
    if (!request) {
      request = { id: newId("cr"), from: user.id, to: target.id, status: "pending", createdAt: now() };
      statements.insertRequest.run(request.id, request.from, request.to, request.createdAt);
    }
    const targetSocket = onlineSocketFor(target.id);
    if (targetSocket) send(targetSocket, { type: "contactRequest", request: { ...request, fromUser: publicUser(user) } });
    return send(socket, { type: "contactRequestSent", request });
  }

  if (type === "acceptContact") {
    const request = requestFromRow(statements.requestByIdForRecipient.get(String(msg.requestId || ""), user.id));
    if (!request) return sendError(socket, "request_not_found", "Contact request was not found.");
    statements.updateRequest.run("accepted", now(), request.id);
    if (!areContacts(request.from, request.to)) statements.insertContact.run(request.from, request.to, now());
    sendContacts(socket, user);
    const fromSocket = onlineSocketFor(request.from);
    if (fromSocket) sendContacts(fromSocket, findUserById(request.from));
    return;
  }

  if (type === "rejectContact") {
    const request = requestFromRow(statements.requestByIdForRecipient.get(String(msg.requestId || ""), user.id));
    if (!request) return sendError(socket, "request_not_found", "Contact request was not found.");
    statements.updateRequest.run("rejected", now(), request.id);
    return sendContacts(socket, user);
  }

  if (type === "removeContact") {
    const target = findUserByUsername(normalizeUsername(msg.blinqId || msg.username || msg.to));
    if (!target || !areContacts(user.id, target.id)) {
      return sendError(socket, "not_contacts", "That user is not in your contacts.");
    }

    statements.deleteContactPair.run(user.id, target.id, target.id, user.id);
    sendContacts(socket, user);
    const targetSocket = onlineSocketFor(target.id);
    if (targetSocket) sendContacts(targetSocket, target);
    return send(socket, { type: "contactRemoved", user: publicUser(target) });
  }

  if (type === "message") {
    if (isRateLimited(`message:${user.id}`, 120, 60 * 1000)) {
      return sendError(socket, "rate_limited", "Please wait a moment before sending more messages.");
    }
    const target = findUserByUsername(normalizeUsername(msg.to));
    if (!target || !areContacts(user.id, target.id)) return sendError(socket, "not_contacts", "You can only message accepted contacts.");
    const body = String(msg.body || "").trim().slice(0, MAX_TEXT_LENGTH);
    if (!body) return;
    const message = { id: String(msg.clientMessageId || "") || newId("m"), kind: "message", from: user.id, to: target.id, body, isHtml: !!msg.isHtml, createdAt: now() };
    const delivery = deliverMessage(message);
    if (delivery !== "failed") {
      send(socket, { type: "messageSent", message });
    }
    return;
  }

  if (type === "imageMessage") {
    if (isRateLimited(`image:${user.id}`, 20, 10 * 60 * 1000)) {
      return sendError(socket, "rate_limited", "Please wait before sending more images.");
    }
    const target = findUserByUsername(normalizeUsername(msg.to));
    if (!target || !areContacts(user.id, target.id)) return sendError(socket, "not_contacts", "You can only message accepted contacts.");
    const mimeType = String(msg.mimeType || "").toLowerCase();
    const ext = extensionForMime(mimeType);
    if (!ext) return sendError(socket, "bad_image", "Only PNG, JPEG, GIF, WEBP, and BMP images are supported.");
    const bytes = Buffer.from(String(msg.data || ""), "base64");
    if (!bytes.length || bytes.length > MAX_IMAGE_BYTES) return sendError(socket, "image_too_large", "Image must be 5 MB or smaller.");
    const imageId = newId("img");
    const fileName = `${imageId}${ext}`;
    fs.writeFileSync(path.join(UPLOAD_DIR, fileName), bytes);
    scheduleUploadDeletion(fileName);
    const message = {
      id: String(msg.clientMessageId || "") || newId("m"),
      kind: "imageMessage",
      from: user.id,
      to: target.id,
      fileName: String(msg.fileName || fileName).slice(0, 120),
      mimeType,
      imageId,
      size: bytes.length,
      data: bytes.toString("base64"),
      createdAt: now(),
    };
    if (!onlineSocketFor(target.id)) {
      deleteUploadFile(fileName);
      return sendError(socket, "offline_image", "Images can only be sent when the contact is online.");
    }
    send(socket, { type: "messageSent", message: { ...message, data: undefined } });
    return deliverMessage(message);
  }

  if (type === "typing" || type === "receipt" || type === "buzz") {
    if (type === "buzz" && isRateLimited(`buzz:${user.id}`, 30, 60 * 1000)) {
      return sendError(socket, "rate_limited", "Please wait before whistling again.");
    }
    const target = findUserByUsername(normalizeUsername(msg.to));
    if (!target || !areContacts(user.id, target.id)) return;
    const targetSocket = onlineSocketFor(target.id);
    if (targetSocket) send(targetSocket, { ...msg, from: blinqId(user.username), fromUser: publicUser(user) });
  }

  if (type === "changePassword") {
    const currentPassword = String(msg.currentPassword || "");
    const newPassword = String(msg.newPassword || "");
    if (!verifyPassword(currentPassword, user)) return sendError(socket, "bad_password", "Current password is incorrect.");
    if (newPassword.length < 8) return sendError(socket, "bad_new_password", "New password must be at least 8 characters.");
    const passwordData = hashPassword(newPassword);
    statements.updateUserPassword.run(passwordData.salt, passwordData.hash, user.id);
    statements.deleteSessionsForUser.run(user.id);
    const token = createToken(user.id);
    send(socket, { type: "passwordChanged" });
    return authenticateSocket(socket, findUserById(user.id), token);
  }

  if (type === "setRecoveryEmail") {
    const password = String(msg.password || "");
    const email = normalizeEmail(msg.email);
    if (!verifyPassword(password, user)) return sendError(socket, "bad_password", "Current password is incorrect.");
    if (!isValidEmail(email)) return sendError(socket, "bad_email", "Enter a valid recovery email address.");
    const existing = findUserByEmail(email);
    if (existing && existing.id !== user.id) return sendError(socket, "email_taken", "That recovery email is already used by another account.");
    statements.updateUserEmail.run(email, user.id);
    return send(socket, { type: "recoveryEmailSet" });
  }

  if (type === "logout") {
    if (socket.sessionToken) {
      statements.deleteSession.run(socket.sessionToken);
    }
    send(socket, { type: "signedOut" });
    clientsByUserId.delete(user.id);
    socket.userId = "";
    socket.sessionToken = "";
    return socket.end();
  }

  if (type === "deleteAccount") {
    if (!verifyPassword(String(msg.password || ""), user)) {
      return sendError(socket, "bad_password", "Password is incorrect.");
    }
    const userId = user.id;
    const contactIds = contactUsersFor(userId).map((contact) => contact.id);
    db.transaction(() => {
      statements.deleteSessionsForUser.run(userId);
      statements.deleteContactsForUser.run(userId, userId);
      statements.deleteRequestsForUser.run(userId, userId);
      statements.deleteQueuedMessagesForUser.run(userId, userId);
      statements.deletePushTokensForUser.run(userId);
      statements.deleteUser.run(userId);
    })();
    send(socket, { type: "accountDeleted" });
    clientsByUserId.delete(userId);
    for (const contactId of contactIds) {
      const contactSocket = onlineSocketFor(contactId);
      if (contactSocket) sendContacts(contactSocket, findUserById(contactId));
    }
    socket.end();
  }
}

function handleConnection(socket) {
  socket.setEncoding("utf8");
  socket.setKeepAlive(true, 30000);
  socket.buffer = "";
  socket.userId = "";
  socket.lastPongAt = Date.now();

  socket.on("data", (chunk) => {
    socket.buffer += chunk;
    if (Buffer.byteLength(socket.buffer, "utf8") > MAX_LINE_BYTES) {
      sendError(socket, "too_large", "Message is too large.");
      socket.destroy();
      return;
    }
    let newline;
    while ((newline = socket.buffer.indexOf("\n")) >= 0) {
      const line = socket.buffer.slice(0, newline).trim();
      socket.buffer = socket.buffer.slice(newline + 1);
      if (!line) continue;
      try {
        handleMessage(socket, JSON.parse(line));
      } catch (error) {
        console.log("request error:", error.message);
        sendError(socket, "bad_request", "Invalid request.");
      }
    }
  });

  socket.on("close", () => {
    if (socket.userId && clientsByUserId.get(socket.userId) === socket) {
      clientsByUserId.delete(socket.userId);
      const user = findUserById(socket.userId);
      if (user) {
        statements.updateUserOnline.run("Offline", now(), user.id);
        broadcastEffectivePresence(findUserById(user.id));
      }
    }
  });

  socket.on("error", (error) => {
    if (!isRoutineSocketError(error)) {
      console.log("socket error:", error.message);
    }
  });
}

const server = net.createServer(handleConnection);

cleanupOldUploads();
cleanupExpiredQueuedMessages();
const cleanupTimer = setInterval(() => {
  cleanupOldUploads();
  cleanupExpiredQueuedMessages();
}, Math.max(60 * 1000, Math.min(UPLOAD_TTL_MS, QUEUED_MESSAGE_TTL_MS)));
if (typeof cleanupTimer.unref === "function") cleanupTimer.unref();

const heartbeatTimer = setInterval(() => {
  const cutoff = Date.now() - 75 * 1000;
  for (const socket of clientsByUserId.values()) {
    if (socket.destroyed) continue;
    if ((socket.lastPongAt || 0) < cutoff) {
      socket.destroy();
      continue;
    }
    send(socket, { type: "ping", at: now() });
  }
}, 30 * 1000);
if (typeof heartbeatTimer.unref === "function") heartbeatTimer.unref();

server.listen(PORT, "0.0.0.0", () => {
  console.log(`Blinq server listening on ${PORT}`);
});
