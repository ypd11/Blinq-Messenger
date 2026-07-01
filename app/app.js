const DEFAULT_WS_URL = location.hostname === "localhost" || location.hostname === "127.0.0.1"
  ? "ws://127.0.0.1:45477/ws"
  : "wss://api.blinqm.net/ws";

const STORAGE = {
  token: "blinq.web.token",
  messages: "blinq.web.messages",
  groups: "blinq.web.groups",
  notifications: "blinq.web.notifications"
};

const els = {
  authView: document.getElementById("auth-view"),
  mainView: document.getElementById("main-view"),
  loginForm: document.getElementById("login-form"),
  loginUsername: document.getElementById("login-username"),
  loginPassword: document.getElementById("login-password"),
  authStatus: document.getElementById("auth-status"),
  connectionLabel: document.getElementById("connection-label"),
  topMenuButton: document.getElementById("top-menu-button"),
  topMenu: document.getElementById("top-menu"),
  profileAvatarButton: document.getElementById("profile-avatar-button"),
  profileAvatar: document.getElementById("profile-avatar"),
  profileName: document.getElementById("profile-name"),
  profileMessageButton: document.getElementById("profile-message-button"),
  profileStatusButton: document.getElementById("profile-status-button"),
  profileStatus: document.getElementById("profile-status"),
  profileEditButton: document.getElementById("profile-edit-button"),
  contactCount: document.getElementById("contact-count"),
  addContactForm: document.getElementById("add-contact-form"),
  addContactInput: document.getElementById("add-contact-input"),
  requestsList: document.getElementById("requests-list"),
  contactList: document.getElementById("contact-list"),
  contactsView: document.getElementById("contacts-view"),
  chatView: document.getElementById("chat-view"),
  emptyChat: document.getElementById("empty-chat"),
  chatContent: document.getElementById("chat-content"),
  backButton: document.getElementById("back-button"),
  chatAvatar: document.getElementById("chat-avatar"),
  chatName: document.getElementById("chat-name"),
  chatStatus: document.getElementById("chat-status"),
  messageList: document.getElementById("message-list"),
  messageForm: document.getElementById("message-form"),
  messageInput: document.getElementById("message-input"),
  contactMenu: document.getElementById("contact-menu"),
  modal: document.getElementById("simple-modal"),
  modalForm: document.getElementById("modal-form"),
  modalTitle: document.getElementById("modal-title"),
  modalMessage: document.getElementById("modal-message"),
  modalBody: document.getElementById("modal-body"),
  modalPrimary: document.getElementById("modal-primary"),
  avatarInput: document.getElementById("avatar-input")
};

const state = {
  ws: null,
  reconnectTimer: 0,
  token: localStorage.getItem(STORAGE.token) || "",
  self: null,
  contacts: [],
  requests: [],
  selectedPeerId: "",
  menuPeerId: "",
  messages: loadJson(STORAGE.messages, {}),
  unread: new Map(),
  collapsedGroups: new Set(loadJson(STORAGE.groups, [])),
  blocked: new Set(),
  blockedInfo: {},
  notificationsEnabled: localStorage.getItem(STORAGE.notifications) === "1"
};

function loadJson(key, fallback) {
  try {
    return JSON.parse(localStorage.getItem(key) || "");
  } catch {
    return fallback;
  }
}

function saveJson(key, value) {
  localStorage.setItem(key, JSON.stringify(value));
}

function accountKey(name) {
  return state.self?.id ? `blinq.web.${name}.${state.self.id}` : `blinq.web.${name}.anonymous`;
}

function loadAccountState() {
  state.blocked = new Set(loadJson(accountKey("blocked"), []));
  state.blockedInfo = loadJson(accountKey("blockedInfo"), {});
}

function saveBlocked() {
  saveJson(accountKey("blocked"), [...state.blocked]);
  saveJson(accountKey("blockedInfo"), state.blockedInfo);
}

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
  return date.toLocaleString([], { month: "short", day: "numeric", hour: "numeric", minute: "2-digit" });
}

function isOnline(user) {
  return !!user?.status && user.status !== "Offline";
}

function statusClass(status) {
  const lower = String(status || "").toLowerCase();
  if (lower === "available") return "available";
  if (lower.includes("busy")) return "busy";
  if (lower.includes("away") || lower.includes("idle")) return "away";
  return "";
}

function saveMessages() {
  saveJson(STORAGE.messages, state.messages);
}

function setStatus(message, isError = false) {
  els.authStatus.textContent = message || "";
  els.authStatus.style.color = isError ? "#dc2626" : "#64748b";
  els.connectionLabel.textContent = message || "Disconnected";
}

function connect() {
  if (state.ws && state.ws.readyState <= WebSocket.OPEN) return;
  clearTimeout(state.reconnectTimer);
  setStatus("Connecting...");
  const ws = new WebSocket(DEFAULT_WS_URL);
  state.ws = ws;

  ws.addEventListener("open", () => {
    setStatus("Connected");
    if (state.token) send({ type: "resume", token: state.token });
  });

  ws.addEventListener("message", (event) => {
    try {
      handleMessage(JSON.parse(event.data));
    } catch {
      setStatus("Invalid server message.", true);
    }
  });

  ws.addEventListener("close", () => {
    setStatus("Disconnected");
    if (state.token) {
      state.reconnectTimer = window.setTimeout(connect, 2500);
    }
  });

  ws.addEventListener("error", () => {
    setStatus("Could not connect to Blinq Internet service.", true);
  });
}

function send(objectValue) {
  if (!state.ws || state.ws.readyState !== WebSocket.OPEN) {
    setStatus("Not connected.", true);
    return false;
  }
  state.ws.send(JSON.stringify(objectValue));
  return true;
}

function handleMessage(message) {
  switch (message.type) {
    case "authenticated":
      state.token = message.token || state.token;
      localStorage.setItem(STORAGE.token, state.token);
      state.self = message.user;
      state.contacts = message.contacts || [];
      state.requests = message.contactRequests || [];
      loadAccountState();
      showMain();
      renderAll();
      setStatus("Server connected");
      break;
    case "contacts":
      state.contacts = message.contacts || [];
      state.requests = message.contactRequests || [];
      renderRequests();
      renderContacts();
      if (state.selectedPeerId) renderChat();
      break;
    case "presence":
      upsertContact(message.user);
      renderContacts();
      if (state.selectedPeerId === message.user?.id) renderChat();
      break;
    case "presenceSet":
      if (message.user?.id === state.self?.id) {
        state.self = { ...state.self, ...message.user };
        renderProfile();
      } else {
        upsertContact(message.user);
        renderContacts();
      }
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
    case "feedbackSent":
      setStatus("Feedback sent");
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
  renderChat();
}

function renderProfile() {
  if (!state.self) return;
  els.profileName.textContent = bestName(state.self);
  els.profileMessageButton.textContent = state.self.personalMessage || "Hi, let's chat!";
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

function visibleContacts() {
  return [...state.contacts]
    .filter((contact) => !state.blocked.has(contact.id))
    .sort((a, b) => {
      const onlineA = isOnline(a);
      const onlineB = isOnline(b);
      if (onlineA !== onlineB) return onlineA ? -1 : 1;
      return bestName(a).localeCompare(bestName(b));
    });
}

function renderContacts() {
  const contacts = visibleContacts();
  const online = contacts.filter(isOnline);
  const offline = contacts.filter((contact) => !isOnline(contact));
  els.contactCount.textContent = `${contacts.length} total`;
  els.contactList.innerHTML = "";
  renderGroup("Online", online);
  renderGroup("Offline", offline);
}

function renderGroup(label, contacts) {
  const isCollapsed = state.collapsedGroups.has(label);
  const header = document.createElement("button");
  header.className = `group-header${isCollapsed ? " collapsed" : ""}`;
  header.type = "button";
  header.dataset.group = label;
  header.innerHTML = `
    <svg viewBox="0 0 24 24" aria-hidden="true"><path d="m7 10 5 5 5-5z"/></svg>
    <span>${escapeHtml(label)} (${contacts.length})</span>
  `;
  els.contactList.appendChild(header);
  if (isCollapsed) return;
  for (const contact of contacts) {
    const unread = state.unread.get(contact.id) || 0;
    const row = document.createElement("button");
    row.className = `contact-row${state.selectedPeerId === contact.id ? " selected" : ""}`;
    row.type = "button";
    row.dataset.peerId = contact.id;
    const status = contact.status || "Offline";
    const message = contact.personalMessage && status !== "Offline" ? ` - ${contact.personalMessage}` : "";
    row.innerHTML = `
      <span class="avatar-frame"><img src="${avatarSrc(contact)}" alt=""></span>
      <span class="contact-main">
        <span class="contact-name">${escapeHtml(bestName(contact))}</span>
        <span class="contact-status ${statusClass(status)}">${escapeHtml(status + message)}</span>
        <span class="last-seen">${status === "Offline" ? "Last seen recently" : "Last seen now"}</span>
      </span>
      ${unread ? `<span class="unread-badge">${unread}</span>` : ""}
    `;
    els.contactList.appendChild(row);
  }
}

function renderChat() {
  const peer = state.contacts.find((contact) => contact.id === state.selectedPeerId);
  els.mainView.classList.toggle("chat-open", !!peer);
  els.emptyChat.classList.toggle("hidden", !!peer);
  els.chatContent.classList.toggle("hidden", !peer);
  if (!peer) return;
  els.chatName.textContent = bestName(peer);
  els.chatStatus.textContent = isOnline(peer) ? `${peer.status || "Available"} - Last seen now` : "Offline";
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
  if (state.blocked.has(peerId)) return;
  const item = {
    id: chatMessage.id,
    body: chatMessage.body || "",
    mine: false,
    createdAt: chatMessage.createdAt || new Date().toISOString()
  };
  state.messages[peerId] = state.messages[peerId] || [];
  state.messages[peerId].push(item);
  saveMessages();
  if (state.selectedPeerId === peerId && document.visibilityState === "visible") {
    send({ type: "receipt", to: from.blinqId, messageId: item.id, status: "Read" });
    renderChat();
  } else {
    state.unread.set(peerId, (state.unread.get(peerId) || 0) + 1);
    send({ type: "receipt", to: from.blinqId, messageId: item.id, status: "Delivered" });
    renderContacts();
    maybeNotify(from, item.body);
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
  closeContactMenu();
  state.selectedPeerId = peerId;
  state.unread.delete(peerId);
  renderContacts();
  renderChat();
  const peer = state.contacts.find((contact) => contact.id === peerId);
  const messages = state.messages[peerId] || [];
  const last = messages[messages.length - 1];
  if (peer && last && !last.mine) {
    send({ type: "receipt", to: peer.blinqId, messageId: last.id, status: "Read" });
  }
}

function clearSession() {
  clearTimeout(state.reconnectTimer);
  state.token = "";
  state.self = null;
  state.contacts = [];
  state.requests = [];
  state.selectedPeerId = "";
  localStorage.removeItem(STORAGE.token);
  showAuth();
  setStatus("");
}

function updatePresence(fields) {
  if (!state.self) return;
  send({
    type: "setPresence",
    status: state.self.status || "Available",
    personalMessage: state.self.personalMessage || "",
    displayName: state.self.displayName || state.self.username || "",
    avatar: state.self.avatar || "",
    themeColor: state.self.themeColor || "#1d9bf0",
    searchable: state.self.searchable !== false,
    ...fields
  });
}

function showPromptModal({ title, message = "", fields = [], primary = "Save" }) {
  els.modalTitle.textContent = title;
  els.modalMessage.textContent = message;
  els.modalPrimary.textContent = primary;
  els.modalBody.innerHTML = fields.map((field) => {
    if (field.type === "select") {
      return `<label>${escapeHtml(field.label)}<select name="${escapeHtml(field.name)}">${field.options.map((option) => `<option value="${escapeHtml(option)}"${option === field.value ? " selected" : ""}>${escapeHtml(option)}</option>`).join("")}</select></label>`;
    }
    return `<label>${escapeHtml(field.label)}<input name="${escapeHtml(field.name)}" value="${escapeHtml(field.value || "")}" ${field.maxLength ? `maxlength="${field.maxLength}"` : ""}></label>`;
  }).join("");
  return new Promise((resolve) => {
    const closeHandler = () => {
      els.modal.removeEventListener("close", closeHandler);
      if (els.modal.returnValue !== "default") return resolve(null);
      const data = new FormData(els.modalForm);
      resolve(Object.fromEntries(data.entries()));
    };
    els.modal.addEventListener("close", closeHandler);
    els.modal.showModal();
  });
}

function showInfoModal(title, message, primary = "OK") {
  return showPromptModal({ title, message, primary });
}

async function editDisplayName() {
  const result = await showPromptModal({
    title: "Display Name",
    fields: [{ label: "Name", name: "displayName", value: bestName(state.self), maxLength: 60 }]
  });
  if (result?.displayName?.trim()) updatePresence({ displayName: result.displayName.trim() });
}

async function editPersonalMessage() {
  const result = await showPromptModal({
    title: "Personal Message",
    fields: [{ label: "Message", name: "personalMessage", value: state.self?.personalMessage || "", maxLength: 140 }]
  });
  if (result) updatePresence({ personalMessage: result.personalMessage || "" });
}

async function editStatus() {
  const result = await showPromptModal({
    title: "Status",
    fields: [{ label: "Status", name: "status", type: "select", value: state.self?.status || "Available", options: ["Available", "Busy", "Away", "Idle", "Invisible"] }]
  });
  if (result?.status) updatePresence({ status: result.status });
}

function chooseAvatar() {
  els.avatarInput.click();
}

async function requestNotifications() {
  if (!("Notification" in window)) {
    await showInfoModal("Notifications", "This browser does not support notifications.");
    return;
  }
  const permission = await Notification.requestPermission();
  state.notificationsEnabled = permission === "granted";
  localStorage.setItem(STORAGE.notifications, state.notificationsEnabled ? "1" : "0");
  await showInfoModal("Notifications", state.notificationsEnabled ? "Browser notifications are enabled." : "Notifications were not enabled.");
}

function maybeNotify(from, body) {
  if (!state.notificationsEnabled || !("Notification" in window) || Notification.permission !== "granted") return;
  if (document.visibilityState === "visible") return;
  const notification = new Notification(bestName(from), {
    body: body || "sent you a message",
    icon: "../assets/appicon.ico",
    tag: `blinq-${from.id}`
  });
  notification.onclick = () => {
    window.focus();
    openChat(from.id);
    notification.close();
  };
}

function blockPeer(peer) {
  if (!peer) return;
  state.blocked.add(peer.id);
  state.blockedInfo[peer.id] = { id: peer.id, name: bestName(peer), blinqId: peer.blinqId || "" };
  saveBlocked();
  if (state.selectedPeerId === peer.id) state.selectedPeerId = "";
  renderContacts();
  renderChat();
  setStatus(`${bestName(peer)} blocked.`);
}

function unblockPeer(peerId) {
  state.blocked.delete(peerId);
  delete state.blockedInfo[peerId];
  saveBlocked();
  renderContacts();
}

async function manageBlockedUsers() {
  els.modalTitle.textContent = "Blocked Users";
  els.modalMessage.textContent = "";
  els.modalPrimary.textContent = "Done";
  els.modalBody.innerHTML = `
    <input id="blocked-search" placeholder="Search blocked users">
    <div class="blocked-list" id="blocked-list"></div>
  `;
  const renderBlocked = () => {
    const list = document.getElementById("blocked-list");
    const needle = document.getElementById("blocked-search").value.trim().toLowerCase();
    const items = Object.values(state.blockedInfo).filter((item) => {
      return !needle || item.name.toLowerCase().includes(needle) || item.blinqId.toLowerCase().includes(needle);
    });
    list.innerHTML = items.length ? items.map((item) => `
      <div class="blocked-row">
        <div><strong>${escapeHtml(item.name)}</strong><span>${escapeHtml(item.blinqId || item.id)}</span></div>
        <button class="secondary-button" type="button" data-unblock="${escapeHtml(item.id)}">Unblock</button>
      </div>
    `).join("") : "<p>No blocked users.</p>";
  };
  els.modalBody.addEventListener("input", renderBlocked);
  els.modalBody.addEventListener("click", (event) => {
    const button = event.target.closest("[data-unblock]");
    if (!button) return;
    unblockPeer(button.dataset.unblock);
    renderBlocked();
  });
  renderBlocked();
  els.modal.showModal();
}

function openContactMenu(peerId, x, y) {
  const peer = state.contacts.find((contact) => contact.id === peerId);
  if (!peer) return;
  state.menuPeerId = peerId;
  const blockButton = els.contactMenu.querySelector('[data-contact-action="block"]');
  blockButton.textContent = state.blocked.has(peerId) ? "Unblock User" : "Block User";
  els.contactMenu.style.left = `${Math.min(x, window.innerWidth - 230)}px`;
  els.contactMenu.style.top = `${Math.min(y, window.innerHeight - 150)}px`;
  els.contactMenu.classList.remove("hidden");
}

function closeContactMenu() {
  els.contactMenu.classList.add("hidden");
}

function toggleTopMenu(force) {
  const shouldOpen = typeof force === "boolean" ? force : els.topMenu.classList.contains("hidden");
  els.topMenu.classList.toggle("hidden", !shouldOpen);
  els.topMenuButton.setAttribute("aria-expanded", shouldOpen ? "true" : "false");
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

els.topMenuButton.addEventListener("click", () => toggleTopMenu());

els.topMenu.addEventListener("click", async (event) => {
  const action = event.target.closest("[data-menu-action]")?.dataset.menuAction;
  if (!action) return;
  toggleTopMenu(false);
  if (action === "displayName") editDisplayName();
  if (action === "personalMessage") editPersonalMessage();
  if (action === "status") editStatus();
  if (action === "avatar") chooseAvatar();
  if (action === "notifications") requestNotifications();
  if (action === "blocked") manageBlockedUsers();
  if (action === "signOut") {
    send({ type: "logout" });
    clearSession();
  }
});

els.profileEditButton.addEventListener("click", editDisplayName);
els.profileMessageButton.addEventListener("click", editPersonalMessage);
els.profileStatusButton.addEventListener("click", editStatus);
els.profileAvatarButton.addEventListener("click", chooseAvatar);

els.avatarInput.addEventListener("change", () => {
  const file = els.avatarInput.files?.[0];
  if (!file) return;
  if (file.size > 220000) {
    showInfoModal("Avatar", "Choose an image smaller than 220 KB.");
    els.avatarInput.value = "";
    return;
  }
  const reader = new FileReader();
  reader.onload = () => {
    const value = String(reader.result || "");
    const base64 = value.includes(",") ? value.split(",").pop() : value;
    updatePresence({ avatar: base64 });
    els.avatarInput.value = "";
  };
  reader.readAsDataURL(file);
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
  const group = event.target.closest("[data-group]");
  if (group) {
    const label = group.dataset.group;
    if (state.collapsedGroups.has(label)) state.collapsedGroups.delete(label);
    else state.collapsedGroups.add(label);
    saveJson(STORAGE.groups, [...state.collapsedGroups]);
    renderContacts();
    return;
  }
  const row = event.target.closest("[data-peer-id]");
  if (row) openChat(row.dataset.peerId);
});

els.contactList.addEventListener("contextmenu", (event) => {
  const row = event.target.closest("[data-peer-id]");
  if (!row) return;
  event.preventDefault();
  openContactMenu(row.dataset.peerId, event.clientX, event.clientY);
});

els.contactList.addEventListener("touchstart", (event) => {
  const row = event.target.closest("[data-peer-id]");
  if (!row) return;
  row._longPressTimer = window.setTimeout(() => {
    const touch = event.touches[0];
    openContactMenu(row.dataset.peerId, touch.clientX, touch.clientY);
  }, 520);
}, { passive: true });

els.contactList.addEventListener("touchend", (event) => {
  const row = event.target.closest("[data-peer-id]");
  if (row?._longPressTimer) clearTimeout(row._longPressTimer);
}, { passive: true });

els.contactMenu.addEventListener("click", async (event) => {
  const action = event.target.closest("[data-contact-action]")?.dataset.contactAction;
  const peer = state.contacts.find((contact) => contact.id === state.menuPeerId);
  if (!action || !peer) return;
  closeContactMenu();
  if (action === "open") openChat(peer.id);
  if (action === "block") {
    if (state.blocked.has(peer.id)) unblockPeer(peer.id);
    else blockPeer(peer);
  }
  if (action === "delete") {
    const result = await showPromptModal({
      title: `Delete ${bestName(peer)}?`,
      message: "This removes the contact and your local chat history.",
      primary: "Delete"
    });
    if (result !== null) {
      delete state.messages[peer.id];
      saveMessages();
      send({ type: "removeContact", to: peer.blinqId });
      if (state.selectedPeerId === peer.id) state.selectedPeerId = "";
      renderChat();
    }
  }
});

document.addEventListener("click", (event) => {
  if (!event.target.closest("#top-menu") && !event.target.closest("#top-menu-button")) toggleTopMenu(false);
  if (!event.target.closest("#contact-menu")) closeContactMenu();
});

els.backButton.addEventListener("click", () => {
  state.selectedPeerId = "";
  els.mainView.classList.remove("chat-open");
  renderContacts();
  renderChat();
});

els.messageForm.addEventListener("submit", (event) => {
  event.preventDefault();
  const peer = state.contacts.find((contact) => contact.id === state.selectedPeerId);
  const body = els.messageInput.value.trim();
  if (!peer || !body || state.blocked.has(peer.id)) return;
  const id = crypto.randomUUID ? crypto.randomUUID() : `web_${Date.now()}`;
  state.messages[peer.id] = state.messages[peer.id] || [];
  state.messages[peer.id].push({ id, body, mine: true, createdAt: new Date().toISOString() });
  saveMessages();
  send({ type: "message", to: peer.blinqId, body, clientMessageId: id });
  els.messageInput.value = "";
  renderChat();
});

document.addEventListener("visibilitychange", () => {
  if (document.visibilityState !== "visible" || !state.selectedPeerId) return;
  const peer = state.contacts.find((contact) => contact.id === state.selectedPeerId);
  const messages = state.messages[state.selectedPeerId] || [];
  const last = messages[messages.length - 1];
  if (peer && last && !last.mine) send({ type: "receipt", to: peer.blinqId, messageId: last.id, status: "Read" });
});

if ("serviceWorker" in navigator) {
  navigator.serviceWorker.register("sw.js").catch(() => {});
}

if (state.token) {
  connect();
} else {
  showAuth();
}
