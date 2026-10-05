import assert from "node:assert/strict";
import { existsSync } from "node:fs";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { chromium } from "playwright-core";

const chrome = process.env.CHROME_PATH || "C:/Program Files/Google/Chrome/Application/chrome.exe";
if (!existsSync(chrome)) throw new Error("Set CHROME_PATH to a Chromium browser for this test.");

const browser = await chromium.launch({ executablePath: chrome, headless: true });
try {
    const page = await browser.newPage();
    await page.goto(pathToFileURL(resolve("../assets/home.html")).href);
    await page.evaluate(() => {
        window.display = "face";
        window.provider = "gemini";
        window.keys = { gemini: false, openai: false };
        window.aiStatus = "idle";
        window.profile = { name: "Hisu", personality: "Thân quen, tinh nghịch vừa phải." };
        window.posts = [];
        window.fetch = async (url, options = {}) => {
            if (url === "/api/status") return new Response(JSON.stringify({
                online: true, ip: "192.168.1.2", display: window.display, gifAvailable: true,
            }));
            if (url === "/api/ai/status") return new Response(JSON.stringify({
                provider: window.provider, configured: window.keys[window.provider], state: window.aiStatus, error: "",
            }));
            if (url === "/api/ai/profile") {
                if (options.method === "POST") {
                    window.profile = JSON.parse(options.body);
                    window.posts.push({ url, body: window.profile });
                }
                return new Response(JSON.stringify(window.profile));
            }
            if (url === "/api/ai/provider") {
                window.posts.push({ url, body: options.body });
                window.provider = options.body;
                return new Response("{}");
            }
            if (url === "/api/ai/key") {
                window.posts.push({ url, body: options.body });
                window.keys[window.provider] = !!options.body;
                return new Response("{}");
            }
            if (url === "/api/ai/ask") {
                window.posts.push({ url, body: options.body });
                return new Response("{}");
            }
            if (url === "/api/ai/subtitle-test") {
                window.posts.push({ url });
                return new Response("{}");
            }
            throw new Error(`Unexpected request: ${url}`);
        };
    });
    await page.addScriptTag({ path: resolve("../assets/home.js") });
    await page.waitForFunction(() => document.querySelector("#aiState").textContent.includes("API key"));
    await page.waitForFunction(() => document.querySelector("#profileName").value === "Hisu");
    assert.equal(await page.locator("#askButton").isDisabled(), true);
    await page.locator("#profileSettings").evaluate(element => element.open = true);
    await page.locator("#profileName").fill("Hisu Nhỏ");
    await page.locator("#profilePersonality").fill("Nói thân quen, tinh nghịch vừa phải.");
    await page.locator("#saveProfile").click();
    await page.waitForFunction(() => document.querySelector("#profileStatus").textContent.includes("Đã lưu"));
    assert.deepEqual(await page.evaluate(() => window.profile), {
        name: "Hisu Nhỏ", personality: "Nói thân quen, tinh nghịch vừa phải.",
    });
    assert.equal(await page.locator("#chatTitle").textContent(), "Trò chuyện với Hisu Nhỏ");
    await page.locator("#profileName").fill("ộ".repeat(50));
    await page.locator("#saveProfile").click();
    assert.match(await page.locator("#profileStatus").textContent(), /quá dài/);
    assert.equal(await page.evaluate(() => window.posts.filter(post => post.url === "/api/ai/profile").length), 1);
    assert.equal(await page.locator("#testSubtitle").isDisabled(), false);
    await page.locator("#testSubtitle").click();
    await page.waitForFunction(() => window.posts.some(post => post.url === "/api/ai/subtitle-test"));

    await page.locator("#aiSettings").evaluate(element => element.open = true);
    await page.locator("#apiKey").fill("gemini-test-secret");
    await page.locator("#keyForm button[type=submit]").click();
    await page.waitForFunction(() => !document.querySelector("#askButton").disabled);
    assert.equal(await page.locator("#apiKey").inputValue(), "");
    assert.equal(await page.locator("body").textContent().then(text => text.includes("gemini-test-secret")), false);

    await page.locator('input[name="aiProvider"][value="openai"]').check();
    await page.waitForFunction(() => document.querySelector("#keySummary").textContent.includes("OpenAI"));
    await page.waitForFunction(() => !document.querySelector("#testSubtitle").disabled);
    assert.equal(await page.locator("#askButton").isDisabled(), true);
    assert.equal(await page.locator("#testSubtitle").isDisabled(), false);
    await page.locator('input[name="aiProvider"][value="gemini"]').check();
    await page.waitForFunction(() => !document.querySelector("#askButton").disabled);
    assert.equal(await page.locator("#keySummary").textContent(), "Gemini API key");

    await page.locator("#question").fill("Xin chào Hisu");
    await page.locator("#askButton").click();
    await page.waitForFunction(() => window.posts.some(post => post.url === "/api/ai/ask"));
    assert.deepEqual(await page.evaluate(() => window.posts.at(-1)), {
        url: "/api/ai/ask", body: "Xin chào Hisu",
    });
    await page.evaluate(async () => { window.aiStatus = "ready"; await refreshAi(); });
    assert.match(await page.locator("#aiState").textContent(), /đang nghĩ/);
    await page.evaluate(async () => { window.aiStatus = "showing"; await refreshAi(); });
    assert.match(await page.locator("#aiState").textContent(), /đang hiện câu trả lời/);
    await page.evaluate(async () => { window.aiStatus = "done"; await refreshAi(); });
    assert.match(await page.locator("#aiState").textContent(), /đã hiện xong/);

    await page.evaluate(() => window.display = "image");
    await page.waitForFunction(() => document.querySelector("#displayState").textContent.includes("ảnh"), null, { timeout: 7000 });
    assert.equal(await page.locator("#askButton").isDisabled(), true);
    assert.equal(await page.locator("#testSubtitle").isDisabled(), true);
    assert.match(await page.locator("#aiState").textContent(), /chỉ hoạt động/);
    await page.setViewportSize({ width: 375, height: 800 });
    const row = await page.locator("#keyForm .chat-row").boundingBox();
    const clear = await page.locator("#clearKey").boundingBox();
    assert.ok(row && clear && clear.x + clear.width <= 375 && row.x >= 0);
    console.log("Home AI: key, ask, and face-only mode passed");
} finally {
    await browser.close();
}
