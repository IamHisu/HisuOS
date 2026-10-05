function signalText(rssi) {
    if (rssi >= -50) {
        return "Rất mạnh";
    }

    if (rssi >= -60) {
        return "Mạnh";
    }

    if (rssi >= -70) {
        return "Trung bình";
    }

    return "Yếu";
}

let selectedSsid = "";
let busy = false;

function selectWifi(network) {
    if (busy) return;
    if (network.saved) {
        connectWifi(network.ssid);
        return;
    }

    selectedSsid = network.ssid;

    const panel = document.getElementById("passwordPanel");
    const ssidLabel = document.getElementById("selectedSsid");
    const password = document.getElementById("wifiPassword");

    ssidLabel.textContent = network.ssid;
    password.value = "";

    panel.classList.remove("hidden");
    password.focus();
}

document.getElementById("togglePassword").addEventListener("click", () => {
    const password = document.getElementById("wifiPassword");
    const button = document.getElementById("togglePassword");

    if (password.type === "password") {
        password.type = "text";
        button.textContent = "Ẩn";
    }
    else {
        password.type = "password";
        button.textContent = "Hiện";
    }
});

async function connectWifi(ssid, password) {
    if (busy) return;

    const button = document.getElementById("connectButton");
    const status = document.getElementById("status");

    busy = true;
    button.disabled = true;
    button.textContent = "Đang kết nối...";
    status.style.display = "block";
    status.textContent = "Đang kết nối " + ssid + "...";

    try {
        const body = new URLSearchParams();
        body.append("ssid", ssid);
        if (password !== undefined) body.append("password", password);

        const response = await fetch("/api/wifi/connect", {
            method: "POST",
            headers: {
                "Content-Type": "application/x-www-form-urlencoded",
            },
            body: body.toString(),
        });

        if (!response.ok) {
            throw new Error("HTTP error");
        }

        const result = await response.json();

        if (result.success) {
            status.style.display = "none";
            document.getElementById("subtitle").textContent = "Hisu đã kết nối Wi-Fi";
            document.getElementById("passwordPanel").classList.add("hidden");
            document.getElementById("networks").classList.add("hidden");
            document.getElementById("refreshButton").classList.add("hidden");
            const url = "http://" + result.ip + "/";
            document.getElementById("homeLink").href = url;
            document.getElementById("homeAddress").textContent = url;
            document.getElementById("homeHandoff").classList.remove("hidden");
            return;
        }
        else {
            status.textContent = result.reason === "save_failed"
                ? "Đã kết nối nhưng không lưu được mạng. Hãy quên một mạng đã lưu rồi thử lại."
                : "Không thể kết nối Wi-Fi. Kiểm tra lại mật khẩu hoặc tín hiệu.";
        }
    }
    catch (error) {
        status.textContent = "Không thể gửi yêu cầu kết nối.";
    }

    button.disabled = false;
    button.textContent = "Kết nối";
    busy = false;
}

document.getElementById("connectButton").addEventListener("click", () => {
    if (!selectedSsid) return;
    connectWifi(selectedSsid, document.getElementById("wifiPassword").value);
});

async function forgetWifi(ssid) {
    if (busy || !confirm("Quên mạng " + ssid + "?")) return;
    busy = true;
    const status = document.getElementById("status");

    try {
        const body = new URLSearchParams({ ssid });
        const response = await fetch("/api/wifi/forget", {
            method: "POST",
            headers: { "Content-Type": "application/x-www-form-urlencoded" },
            body: body.toString(),
        });
        if (!response.ok) throw new Error("Forget failed");
        busy = false;
        await scanWifi();
    }
    catch (error) {
        status.style.display = "block";
        status.textContent = "Không thể quên mạng Wi-Fi.";
        busy = false;
    }
}

async function scanWifi() {
    if (busy) return;
    busy = true;
    const status = document.getElementById("status");
    const list = document.getElementById("networks");
    document.getElementById("passwordPanel").classList.add("hidden");
    selectedSsid = "";

    status.style.display = "block";
    status.textContent = "Đang quét Wi-Fi...";
    list.innerHTML = "";

    try {
        const response = await fetch("/api/wifi/scan");

        if (!response.ok) {
            throw new Error("Wi-Fi scan failed");
        }

        const data = await response.json();
        busy = false;

        status.style.display = "none";

        if (!data.networks.length) {
            status.style.display = "block";
            status.textContent = "Không tìm thấy Wi-Fi";
            return;
        }

        data.networks.forEach(network => {
            const row = document.createElement("div");
            row.className = "network-row";
            const button = document.createElement("button");
            button.className = "network";
            button.type = "button";
            button.onclick = () => selectWifi(network);

            const ssid = document.createElement("div");
            ssid.className = "ssid";
            ssid.textContent = "📶 " + network.ssid;

            const info = document.createElement("div");
            info.className = "info";
            info.textContent = signalText(network.rssi) + " · " + network.rssi + " dBm · Kênh " + network.channel + (network.saved ? " · Đã lưu" : "");

            button.appendChild(ssid);
            button.appendChild(info);
            row.appendChild(button);
            if (network.saved) {
                const forget = document.createElement("button");
                forget.className = "forget";
                forget.type = "button";
                forget.textContent = "Quên";
                forget.setAttribute("aria-label", "Quên mạng " + network.ssid);
                forget.onclick = () => forgetWifi(network.ssid);
                row.appendChild(forget);
            }
            list.appendChild(row);
        });
    }
    catch (error) {
        busy = false;
        status.style.display = "block";
        status.textContent = "Không thể quét Wi-Fi";
    }
}

document.getElementById("refreshButton").addEventListener("click", scanWifi);

scanWifi();
