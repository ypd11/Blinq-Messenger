const DEFAULT_WS_URL = location.hostname === "localhost" || location.hostname === "127.0.0.1"
  ? "ws://127.0.0.1:45477/ws"
  : "wss://api.blinqm.net/ws";

const els = {
  authView: document.getElementById("auth-view"),
  mainView: document.getElementById("main-view"),
  loginForm: document.getElementById("login-form"),
  loginUsername: document.getElementById("login-username"),
  loginPassword: document.getElementById("login-password"),
  authStatus: document.getElementById("auth-status"),
  connectionLabel: document.getElementById("connection-label"),
  signOut: document.getElementById("sign-out-button"),
  profileAvatar: document.getElementById("profile-avatar"),
  profileName: document.getElementById("profile-name"),
  profileMessage: document.getElementById("profile-message"),
  profileStatus: document.getElementById("profile-status"),
  contactCount: document.getElementById("contact-count"),
  addContactForm: document.getElementById("add-contact-form"),
  addContactInput: document.getElementById("add-contact-input"),
  requestsList: document.getElementById("requests-list"),
  contactList: document.getElementById("contact-list"),
  contactsView: document.getElementById("contacts-view"),
  chatView: document.getElementById("chat-view"),
  backButton: document.getElementById("back-button"),
  chatAvatar: document.getElementById("chat-avatar"),
  chatName: document.getElementById("chat-name"),
  chatStatus: document.getElementById("chat-status"),
  messageList: document.getElementById("message-list"),
  messageForm: document.getElementById("message-form"),
  messageInput: document.getElementById("message-input")
};

const state = {
  ws: null,
  token: localStorage.getItem("blinq.web.token") || "",
  self: null,
  contacts: [],
  requests: [],
  selectedPeerId: "",
  messages: loadMessages(),
  unread: new Map()
};

function normalizeBlinqId(value) {
  const trimmed = String(value || "").trim().toLowerCase();
  return trimmed.includes("@") ? trimmed : `${trimmed}@blinqm.net`;
}

function bestName(user) {
  return user?.displayName || user?.username || user?.blinqId || "Blinq User";
}

function avatarSrc(user) {
  return user?.avatar ? `data:image/png;base64,${user.avatar}` : "../assets/avatar_placeholder.png";
}

function escapeHtml(value) {
  return String(value || "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

function formatTime(dateValue) {
  const date = dateValue ? new Date(dateValue) : new Date();
  return date.toLocaleTimeString([], { hour: "numeric", minute: "2-digit" });
}

function statusClass(status) {
  const lower = String(status || "").toLowerCase();
  if (lower === "available") return "available";
  if (lower.includes("busy")) return "busy";
  if (lower.includes("away") || lower.includes("idle")) return "away";
  return "";
}

function loadMessages() {
  try {
    return JSON.parse(localStorage.getItem("blinq.web.messages") || "{}");
  } catch {
    return {};
  }
}

function saveMessages() {
  localStorage.setItem("blinq.web.messages", JSON.stringify(state.messages));
}

function setStatus(message, isError = false) {
  els.authStatus.textContent = message || "";
  els.authStatus.style.color = isError ? "#dc2626" : "#64748b";
  els.connectionLabel.textContent = message || "Disconnected";
}

function connect() {
  if (state.ws && state.ws.readyState <= 1) return;
  setStatus("Connecting...");
  const ws = new WebSocket(DEFAULT_WS_URL);
  state.ws = ws;

  ws.addEventListener("open", () => {
    setStatus("Connected");
    if (state.token) send({ type: "resume", token: state.token });
  });

  ws.addEventListener("message", (event) => {
    handleMessage(JSON.parse(event.data));
  });

  ws.addEventListener("close", () => {
    setStatus("Disconnected");
  });

  ws.addEventListener("error", () => {
    setStatus("Could not connect to Blinq Internet service.", true);
  });
}

function send(objectValue) {
  if (!state.ws || state.ws.readyState !== WebSocket.OPEN) {
    setStatus("Not connected.", true);
    return;
  }
  state.ws.send(JSON.stringify(objectValue));
}

function handleMessage(message) {
  switch (message.type) {
    case "authenticated":
      state.token = message.token || state.token;
      localStorage.setItem("blinq.web.token", state.token);
      state.self = message.user;
      state.contacts = message.contacts || [];
      state.requests = message.contactRequests || [];
      showMain();
      renderAll();
      setStatus("Server connected");
      break;
    case "contacts":
      state.contacts = message.contacts || [];
      state.requests = message.contactRequests || [];
      renderContacts();
      break;
    case "presence":
    case "presenceSet":
      upsertContact(message.user);
      renderContacts();
      if (state.selectedPeerId === message.user?.id) renderChat();
      break;
    case "contactRequest":
      state.requests.push(message.request);
      renderRequests();
      break;
    case "message":
      receiveChatMessage(message);
      break;
    case "messageSent":
      markMessageSent(message.message);
      break;
    case "receipt":
      break;
    case "ping":
      send({ type: "pong" });
      break;
    case "signedOut":
      clearSession();
      break;
    case "error":
      setStatus(message.message || "Server error.", true);
      break;
  }
}

function upsertContact(user) {
  if (!user?.id) return;
  const index = state.contacts.findIndex((contact) => contact.id === user.id);
  if (index >= 0) state.contacts[index] = { ...state.contacts[index], ...user };
  else state.contacts.push(user);
}

function showMain() {
  els.authView.classList.add("hidden");
  els.mainView.classList.remove("hidden");
}

function showAuth() {
  els.mainView.classList.add("hidden");
  els.authView.classList.remove("hidden");
}

function renderAll() {
  renderProfile();
  renderRequests();
  renderContacts();
}

function renderProfile() {
  if (!state.self) return;
  els.profileName.textContent = bestName(state.self);
  els.profileMessage.textContent = state.self.personalMessage || "Hi, let's chat!";
  els.profileStatus.textContent = state.self.status || "Available";
  els.profileAvatar.src = avatarSrc(state.self);
}

function renderRequests() {
  els.requestsList.innerHTML = "";
  for (const request of state.requests) {
    const from = request.fromUser || {};
    const card = document.createElement("div");
    card.className = "request-card";
    card.innerHTML = `
      <strong>${escapeHtml(bestName(from))} wants to add you.</strong>
      <div class="request-actions">
        <button type="button" data-accept="${escapeHtml(request.id)}">Accept</button>
        <button type="button" data-reject="${escapeHtml(request.id)}">Reject</button>
      </div>
    `;
    els.requestsList.appendChild(card);
  }
}

function renderContacts() {
  const contacts = [...state.contacts].sort((a, b) => {
    const onlineA = a.status && a.status !== "Offline";
    const onlineB = b.status && b.status !== "Offline";
    if (onlineA !== onlineB) return onlineA ? -1 : 1;
    return bestName(a).localeCompare(bestName(b));
  });
  const online = contacts.filter((contact) => contact.status && contact.status !== "Offline");
  const offline = contacts.filter((contact) => !contact.status || contact.status === "Offline");
  els.contactCount.textContent = `${contacts.length} total`;
  els.contactList.innerHTML = "";
  renderGroup("Online", online);
  renderGroup("Offline", offline);
}

function renderGroup(label, contacts) {
  const header = document.createElement("div");
  header.className = "group-header";
  header.textContent = `${label} (${contacts.length})`;
  els.contactList.appendChild(header);
  for (const contact of contacts) {
    const unread = state.unread.get(contact.id) || 0;
    const row = document.createElement("button");
    row.className = "contact-row";
    row.type = "button";
    row.dataset.peerId = contact.id;
    const status = contact.status || "Offline";
    row.innerHTML = `
      <span class="avatar-frame"><img src="${avatarSrc(contact)}" alt=""></span>
      <span class="contact-main">
        <span class="contact-name">${escapeHtml(bestName(contact))}</span>
        <span class="contact-status ${statusClass(status)}">${escapeHtml(status)}${contact.personalMessage && status !== "Offline" ? ` · ${escapeHtml(contact.personalMessage)}` : ""}</span>
        <span class="last-seen">${status === "Offline" ? "Last seen recently" : "Last seen now"}</span>
      </span>
      ${unread ? `<span class="unread-badge">${unread}</span>` : ""}
    `;
    els.contactList.appendChild(row);
  }
}

function renderChat() {
  const peer = state.contacts.find((contact) => contact.id === state.selectedPeerId);
  if (!peer) return;
  els.contactsView.classList.add("hidden");
  els.chatView.classList.remove("hidden");
  els.chatName.textContent = bestName(peer);
  els.chatStatus.textContent = `${peer.status || "Offline"}${peer.status && peer.status !== "Offline" ? " · Last seen now" : ""}`;
  els.chatAvatar.src = avatarSrc(peer);
  const messages = state.messages[peer.id] || [];
  els.messageList.innerHTML = messages.map((message) => `
    <div class="message ${message.mine ? "mine" : ""}">
      <div class="message-meta">${escapeHtml(message.mine ? "Me" : bestName(peer))} <span class="message-time">${formatTime(message.createdAt)}</span></div>
      <div class="message-body">${escapeHtml(message.body)}</div>
    </div>
  `).join("");
  els.messageList.scrollTop = els.messageList.scrollHeight;
}

function receiveChatMessage(message) {
  const from = message.fromUser || {};
  const chatMessage = message.message || {};
  upsertContact(from);
  const peerId = from.id || chatMessage.from;
  const item = {
    id: chatMessage.id,
    body: chatMessage.body || "",
    mine: false,
    createdAt: chatMessage.createdAt || new Date().toISOString()
  };
  state.messages[peerId] = state.messages[peerId] || [];
  state.messages[peerId].push(item);
  saveMessages();
  if (state.selectedPeerId === peerId) {
    send({ type: "receipt", to: from.blinqId, messageId: item.id, status: "Read" });
    renderChat();
  } else {
    state.unread.set(peerId, (state.unread.get(peerId) || 0) + 1);
    send({ type: "receipt", to: from.blinqId, messageId: item.id, status: "Delivered" });
    renderContacts();
  }
}

function markMessageSent(sent) {
  if (!sent?.to) return;
  const peer = state.contacts.find((contact) => contact.id === sent.to || contact.blinqId === sent.to);
  if (!peer) return;
  const list = state.messages[peer.id] || [];
  const found = list.find((message) => message.id === sent.id);
  if (found) found.createdAt = sent.createdAt || found.createdAt;
  saveMessages();
}

function openChat(peerId) {
  state.selectedPeerId = peerId;
  state.unread.delete(peerId);
  renderContacts();
  renderChat();
}

function clearSession() {
  state.token = "";
  state.self = null;
  state.contacts = [];
  state.requests = [];
  localStorage.removeItem("blinq.web.token");
  showAuth();
  setStatus("");
}

els.loginForm.addEventListener("submit", (event) => {
  event.preventDefault();
  connect();
  const login = () => send({
    type: "login",
    username: normalizeBlinqId(els.loginUsername.value).replace("@blinqm.net", ""),
    password: els.loginPassword.value
  });
  if (state.ws?.readyState === WebSocket.OPEN) login();
  else state.ws?.addEventListener("open", login, { once: true });
});

els.signOut.addEventListener("click", () => {
  send({ type: "logout" });
  clearSession();
});

els.addContactForm.addEventListener("submit", (event) => {
  event.preventDefault();
  const value = els.addContactInput.value.trim();
  if (!value) return;
  send({ type: "addContact", to: normalizeBlinqId(value) });
  els.addContactInput.value = "";
});

els.requestsList.addEventListener("click", (event) => {
  const accept = event.target.closest("[data-accept]");
  const reject = event.target.closest("[data-reject]");
  if (accept) send({ type: "acceptContact", requestId: accept.dataset.accept });
  if (reject) send({ type: "rejectContact", requestId: reject.dataset.reject });
});

els.contactList.addEventListener("click", (event) => {
  const row = event.target.closest("[data-peer-id]");
  if (row) openChat(row.dataset.peerId);
});

els.backButton.addEventListener("click", () => {
  state.selectedPeerId = "";
  els.chatView.classList.add("hidden");
  els.contactsView.classList.remove("hidden");
});

els.messageForm.addEventListener("submit", (event) => {
  event.preventDefault();
  const peer = state.contacts.find((contact) => contact.id === state.selectedPeerId);
  const body = els.messageInput.value.trim();
  if (!peer || !body) return;
  const id = crypto.randomUUID ? crypto.randomUUID() : `web_${Date.now()}`;
  state.messages[peer.id] = state.messages[peer.id] || [];
  state.messages[peer.id].push({ id, body, mine: true, createdAt: new Date().toISOString() });
  saveMessages();
  send({ type: "message", to: peer.blinqId, body, clientMessageId: id });
  els.messageInput.value = "";
  renderChat();
});

if ("serviceWorker" in navigator) {
  navigator.serviceWorker.register("sw.js").catch(() => {});
}

if (state.token) {
  connect();
} else {
  showAuth();
}
