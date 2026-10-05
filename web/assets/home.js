const connection = document.getElementById("connection");
const displayState = document.getElementById("displayState");
const uploadStatus = document.getElementById("uploadStatus");
const gifNotice = document.getElementById("gifNotice");
const picker = document.getElementById("imageFile");
const pickerLabel = picker.closest(".file-picker");
const preview = document.getElementById("preview");
const screenEmpty = document.getElementById("screenEmpty");
const faceButton = document.getElementById("showFace");
const askForm = document.getElementById("askForm");
const question = document.getElementById("question");
const askButton = document.getElementById("askButton");
const aiState = document.getElementById("aiState");
const testSubtitle = document.getElementById("testSubtitle");
const chatTitle = document.getElementById("chatTitle");
const profileForm = document.getElementById("profileForm");
const profileName = document.getElementById("profileName");
const profilePersonality = document.getElementById("profilePersonality");
const profileStatus = document.getElementById("profileStatus");
const saveProfile = document.getElementById("saveProfile");
const keyForm = document.getElementById("keyForm");
const apiKey = document.getElementById("apiKey");
const keyStatus = document.getElementById("keyStatus");
const clearKey = document.getElementById("clearKey");
const providerInputs = [...document.querySelectorAll('input[name="aiProvider"]')];
const keySummary = document.getElementById("keySummary");
let previewUrl = null;
let busy = false;
let refreshing = false;
let currentDisplay = "face";
let aiConfigured = false;
let aiWorking = false;
let aiProvider = "gemini";
let providerSwitching = false;
let aiName = "Hisu";

function providerLabel() { return aiProvider === "openai" ? "OpenAI" : "Gemini"; }

function setMessage(text, kind) {
    uploadStatus.textContent = text;
    uploadStatus.className = kind || "";
}

function renderScreen() {
    const showLocal = previewUrl && currentDisplay !== "face";
    preview.classList.toggle("hidden", !showLocal);
    screenEmpty.classList.toggle("hidden", showLocal);
    screenEmpty.textContent = currentDisplay === "face" ? "Đang hiện mặt Hisu"
        : currentDisplay === "gif" ? "Đang phát GIF trên Hisu" : "Đang hiện ảnh trên Hisu";
}

function setBusy(value) {
    busy = value;
    picker.disabled = faceButton.disabled = value;
    pickerLabel.classList.toggle("disabled", value);
}

function renderAi() {
    const enabled = currentDisplay === "face" && aiConfigured && !aiWorking && !providerSwitching;
    question.disabled = currentDisplay !== "face" || aiWorking || providerSwitching;
    askButton.disabled = !enabled;
    testSubtitle.disabled = currentDisplay !== "face" || aiWorking || providerSwitching;
    if (currentDisplay !== "face") aiState.textContent = "AI chỉ hoạt động khi hiện mặt Hisu.";
    else if (!aiConfigured) aiState.textContent = `Hãy lưu ${providerLabel()} API key để bắt đầu.`;
    else if (aiWorking) aiState.textContent = "Hisu đang nghĩ...";
    keySummary.textContent = `${providerLabel()} API key`;
    apiKey.placeholder = aiProvider === "openai" ? "sk-..." : "Gemini API key";
    for (const input of providerInputs) {
        input.checked = input.value === aiProvider;
        input.disabled = aiWorking || providerSwitching;
    }
    apiKey.disabled = aiWorking || providerSwitching;
    keyForm.querySelector('button[type="submit"]').disabled = aiWorking || providerSwitching;
    clearKey.disabled = aiWorking || providerSwitching;
    saveProfile.disabled = aiWorking || providerSwitching;
}

async function refreshProfile() {
    try {
        const response = await fetch("/api/ai/profile", { cache: "no-store" });
        if (!response.ok) throw new Error("Không đọc được tên và tính cách.");
        const profile = await response.json();
        aiName = profile.name;
        profileName.value = profile.name;
        profilePersonality.value = profile.personality;
        chatTitle.textContent = `Trò chuyện với ${aiName}`;
    } catch (error) { profileStatus.textContent = error.message; }
}

async function refreshAi() {
    try {
        const response = await fetch("/api/ai/status", { cache: "no-store", signal: AbortSignal.timeout(4000) });
        if (!response.ok) throw new Error("status");
        const status = await response.json();
        if (!providerSwitching) aiProvider = status.provider === "openai" ? "openai" : "gemini";
        aiConfigured = status.configured;
        aiWorking = status.state === "working" || status.state === "ready";
        renderAi();
        if (currentDisplay === "face" && aiConfigured && !aiWorking)
            aiState.textContent = status.state === "error" ? status.error :
                status.state === "showing" ? "Hisu đang hiện câu trả lời trên màn hình." :
                status.state === "done" ? "Hisu đã hiện xong câu trả lời." : "Sẵn sàng trò chuyện.";
    } catch (_) {
        aiState.textContent = "Không đọc được trạng thái AI.";
    }
}

async function refreshStatus() {
    if (refreshing || busy) return;
    refreshing = true;
    try {
        const response = await fetch("/api/status", { cache: "no-store", signal: AbortSignal.timeout(4000) });
        if (!response.ok) throw new Error("status");
        const status = await response.json();
        connection.textContent = status.online ? status.ip : "Đã ngắt Wi-Fi";
        connection.className = status.online ? "online" : "offline";
        currentDisplay = status.display === "gif" || status.display === "image" ? status.display : "face";
        displayState.textContent = currentDisplay === "gif" ? "Đang phát GIF" :
            currentDisplay === "image" ? "Đang hiện ảnh" : "Đang hiện mặt Hisu";
        gifNotice.textContent = status.gifAvailable ? "" : "GIF tạm không khả dụng do thiếu bộ nhớ.";
        renderScreen();
        renderAi();
    } catch (_) {
        connection.textContent = "Không kết nối được";
        connection.className = "offline";
    } finally {
        refreshing = false;
    }
}

function imageBytes(image) {
    const canvas = document.createElement("canvas");
    canvas.width = canvas.height = 240;
    const context = canvas.getContext("2d", { willReadFrequently: true });
    if (!context) throw new Error("Trình duyệt không hỗ trợ Canvas.");
    context.fillStyle = "#000";
    context.fillRect(0, 0, 240, 240);
    const scale = Math.min(240 / image.naturalWidth, 240 / image.naturalHeight);
    const width = Math.max(1, Math.round(image.naturalWidth * scale));
    const height = Math.max(1, Math.round(image.naturalHeight * scale));
    context.drawImage(image, (240 - width) / 2, (240 - height) / 2, width, height);
    const pixels = context.getImageData(0, 0, 240, 240).data;
    const bytes = new Uint8Array(240 * 240 * 2);
    for (let i = 0, j = 0; i < pixels.length; i += 4, j += 2) {
        const value = ((pixels[i] & 0xf8) << 8) | ((pixels[i + 1] & 0xfc) << 3) | (pixels[i + 2] >> 3);
        bytes[j] = value & 0xff;
        bytes[j + 1] = value >> 8;
    }
    return bytes;
}

function loadImage(url) {
    return new Promise((resolve, reject) => {
        const image = new Image();
        image.onload = () => resolve(image);
        image.onerror = () => reject(new Error("Không đọc được ảnh."));
        image.src = url;
    });
}

function clearPreview() {
    if (previewUrl) URL.revokeObjectURL(previewUrl);
    previewUrl = null;
    preview.removeAttribute("src");
}

async function upload(file) {
    if (busy) return;
    const gif = /\.gif$/i.test(file.name) || file.type === "image/gif";
    const still = /\.(jpe?g|png)$/i.test(file.name) || file.type === "image/jpeg" || file.type === "image/png";
    if (!gif && !still) {
        setMessage("Chỉ nhận JPG, PNG hoặc GIF.", "error");
        return;
    }
    const sourceLimit = 8 * 1024 * 1024;
    if (gif && file.size > sourceLimit) {
        setMessage("GIF gốc tối đa 8 MB.", "error");
        return;
    }

    setBusy(true);
    setMessage("Đang chuẩn bị ảnh...");
    try {
        clearPreview();
        previewUrl = URL.createObjectURL(file);
        preview.src = previewUrl;
        const image = await loadImage(previewUrl);
        let body;
        if (!gif) body = imageBytes(image);
        else {
            if (typeof FriendGifResize === "undefined") throw new Error("Không tải được bộ xử lý GIF. Hãy mở lại trang.");
            body = await FriendGifResize.resizeGif(file, (done, total) => {
                setMessage(`Đang xử lý GIF: ${done}/${total} frame...`);
            });
            if (body !== file) {
                URL.revokeObjectURL(previewUrl);
                previewUrl = URL.createObjectURL(body);
                preview.src = previewUrl;
            }
        }
        setMessage("Đang gửi tới Hisu...");
        const response = await fetch(gif ? "/api/media/gif" : "/api/media/still", {
            method: "POST", headers: { "Content-Type": "application/octet-stream" }, body,
        });
        if (!response.ok) throw new Error(response.status === 503
            ? "Hisu đang bận hoặc thiếu bộ nhớ. Thử lại sau vài giây."
            : response.status === 507 ? "Không lưu được ảnh vào bộ nhớ flash của Hisu."
            : "Tải ảnh thất bại (HTTP " + response.status + ").");
        currentDisplay = gif ? "gif" : "image";
        renderScreen();
        renderAi();
        setMessage(gif && body !== file ? `Đã gửi GIF chuẩn hóa (${Math.round(body.size / 1024)} KB).`
            : "Đã gửi ảnh tới màn hình.", "ok");
        setTimeout(refreshStatus, 500);
    } catch (error) {
        clearPreview();
        renderScreen();
        setMessage(error.message, "error");
    } finally {
        setBusy(false);
    }
}

picker.addEventListener("change", () => {
    const file = picker.files[0];
    if (file) upload(file);
    picker.value = "";
});

faceButton.addEventListener("click", async () => {
    if (busy) return;
    setBusy(true);
    try {
        const response = await fetch("/api/media/face", { method: "POST" });
        if (!response.ok) throw new Error(response.status === 507
            ? "Không lưu được lựa chọn mặt Hisu vào flash."
            : "Không chuyển được màn hình.");
        clearPreview();
        currentDisplay = "face";
        renderScreen();
        renderAi();
        setMessage("Đã trở lại mặt Hisu.", "ok");
        setTimeout(refreshStatus, 500);
    } catch (error) {
        setMessage(error.message, "error");
    } finally {
        setBusy(false);
    }
});

askForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    const text = question.value.trim();
    if (!text || currentDisplay !== "face" || aiWorking || !aiConfigured) return;
    aiWorking = true;
    renderAi();
    try {
        const response = await fetch("/api/ai/ask", {
            method: "POST", headers: { "Content-Type": "text/plain; charset=utf-8" }, body: text,
        });
        if (!response.ok) throw new Error(response.status === 409 ? "AI đang bận hoặc Hisu đang bận show ảnh."
            : response.status === 428 ? `Hãy lưu ${providerLabel()} API key trước.` : "Không gửi được câu hỏi.");
        question.value = "";
        aiState.textContent = "Hisu đang nghĩ...";
    } catch (error) {
        aiWorking = false;
        renderAi();
        aiState.textContent = error.message;
    }
});

testSubtitle.addEventListener("click", async () => {
    if (currentDisplay !== "face" || aiWorking) return;
    try {
        const response = await fetch("/api/ai/subtitle-test", { method: "POST" });
        if (!response.ok) throw new Error("Hisu đang bận show ảnh rồi.");
        aiState.textContent = "Đã gửi phụ đề thử tới Hisu.";
        refreshAi();
    } catch (error) { aiState.textContent = error.message; }
});

profileForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (aiWorking || providerSwitching) return;
    const name = profileName.value.trim();
    const personality = profilePersonality.value.trim();
    const bytes = new TextEncoder();
    if (!name || !personality || bytes.encode(name).length > 96 || bytes.encode(personality).length > 512) {
        profileStatus.textContent = "Tên hoặc tính cách quá dài hoặc để trống.";
        return;
    }
    saveProfile.disabled = true;
    try {
        const response = await fetch("/api/ai/profile", {
            method: "POST", headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ name, personality }),
        });
        if (!response.ok) throw new Error(response.status === 409 ? "Hisu đang trả lời, thử lại sau."
            : "Không lưu được tên và tính cách.");
        aiName = name;
        chatTitle.textContent = `Trò chuyện với ${aiName}`;
        profileStatus.textContent = "Đã lưu trên Hisu.";
    } catch (error) { profileStatus.textContent = error.message; }
    finally { renderAi(); }
});

keyForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    const key = apiKey.value.trim();
    if (!key) return;
    try {
        const response = await fetch("/api/ai/key", { method: "POST", body: key });
        if (!response.ok) throw new Error("Không lưu được API key.");
        apiKey.value = "";
        keyStatus.textContent = "Đã lưu key trên Hisu.";
        refreshAi();
    } catch (error) { keyStatus.textContent = error.message; }
});

clearKey.addEventListener("click", async () => {
    try {
        const response = await fetch("/api/ai/key", { method: "POST", body: "" });
        if (!response.ok) throw new Error("Không xóa được API key.");
        apiKey.value = "";
        keyStatus.textContent = "Đã xóa key.";
        refreshAi();
    } catch (error) { keyStatus.textContent = error.message; }
});

for (const input of providerInputs) input.addEventListener("change", async () => {
    if (!input.checked || aiWorking || providerSwitching) return;
    const previous = aiProvider;
    aiProvider = input.value;
    aiConfigured = false;
    providerSwitching = true;
    renderAi();
    try {
        const response = await fetch("/api/ai/provider", { method: "POST", body: input.value });
        if (!response.ok) throw new Error("Không đổi được dịch vụ AI.");
        apiKey.value = "";
        keyStatus.textContent = "";
    } catch (error) {
        aiProvider = previous;
        keyStatus.textContent = error.message;
    }
    finally {
        providerSwitching = false;
        await refreshAi();
    }
});

refreshStatus();
setInterval(refreshStatus, 5000);
refreshAi();
setInterval(refreshAi, 1500);
refreshProfile();
