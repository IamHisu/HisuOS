import { parseGIF, decompressFrame } from "gifuct-js";
import { GIFEncoder, quantize, applyPalette } from "gifenc";

const MAX_OUTPUT_BYTES = 1024 * 1024;
const MAX_INPUT_BYTES = 8 * MAX_OUTPUT_BYTES;
const MAX_SOURCE_PIXELS = 1500000;
const MAX_FRAMES = 180;

function canvas(width, height) {
    const element = document.createElement("canvas");
    element.width = width;
    element.height = height;
    const context = element.getContext("2d", { willReadFrequently: true });
    if (!context) throw new Error("Trình duyệt không hỗ trợ xử lý GIF.");
    return { element, context };
}

function restorePrevious(context, previous) {
    if (!previous) return;
    if (previous.disposalType === 3 && previous.snapshot) {
        context.putImageData(previous.snapshot, 0, 0);
    } else if (previous.disposalType === 2) {
        const { left, top, width, height } = previous.dims;
        context.clearRect(left, top, width, height);
    }
}

async function encodeAtSize(gif, frames, width, height, repeat, notify) {
    const source = canvas(gif.lsd.width, gif.lsd.height);
    const patch = canvas(1, 1);
    const output = canvas(width, height);
    const encoder = GIFEncoder();
    let previous = null;

    for (let i = 0; i < frames.length; i++) {
        const frame = decompressFrame(frames[i], gif.gct, true);
        if (!frame || !frame.patch || frame.dims.width < 1 || frame.dims.height < 1) {
            throw new Error("Không giải mã được một frame GIF.");
        }
        restorePrevious(source.context, previous);

        const snapshot = frame.disposalType === 3
            ? source.context.getImageData(0, 0, source.element.width, source.element.height) : null;
        patch.element.width = frame.dims.width;
        patch.element.height = frame.dims.height;
        patch.context.putImageData(new ImageData(frame.patch, frame.dims.width, frame.dims.height), 0, 0);
        source.context.drawImage(patch.element, frame.dims.left, frame.dims.top);

        output.context.fillStyle = "#000";
        output.context.fillRect(0, 0, width, height);
        output.context.drawImage(source.element, 0, 0, width, height);
        const pixels = output.context.getImageData(0, 0, width, height).data;
        const palette = quantize(pixels, 64);
        const indexed = applyPalette(pixels, palette);
        encoder.writeFrame(indexed, width, height, {
            palette,
            delay: Math.max(20, frame.delay || 100),
            repeat,
            dispose: 1,
        });
        previous = { ...frame, snapshot };

        if (i % 4 === 3) {
            notify?.(i + 1, frames.length);
            await new Promise(resolve => setTimeout(resolve, 0));
        }
    }
    encoder.finish();
    return new Blob([encoder.bytes()], { type: "image/gif" });
}

export async function resizeGif(file, notify) {
    if (file.size > MAX_INPUT_BYTES) {
        throw new Error("GIF gốc tối đa 8 MB để trình duyệt xử lý an toàn.");
    }
    let gif;
    try {
        gif = parseGIF(await file.arrayBuffer());
    } catch (_) {
        throw new Error("Không đọc được GIF.");
    }
    const width = gif.lsd.width;
    const height = gif.lsd.height;
    if (!width || !height || width * height > MAX_SOURCE_PIXELS) {
        throw new Error("GIF quá lớn để xử lý trên trình duyệt này.");
    }
    const frames = gif.frames.filter(frame => frame.image);
    if (!frames.length || frames.length > MAX_FRAMES) {
        throw new Error("GIF cần có từ 1 đến 180 frame.");
    }
    const needsCompositing = frames.length > 1 && frames.some(frame =>
        frame.gce?.extras.disposal === 2 || frame.gce?.extras.disposal === 3 ||
        frame.gce?.extras.transparentColorGiven);
    if (width <= 240 && height <= 240 && file.size <= MAX_OUTPUT_BYTES && !needsCompositing) {
        return file;
    }
    const loop = gif.frames.find(frame => frame.application?.id === "NETSCAPE2.0")?.application.blocks;
    const repeat = loop?.[0] === 1 ? loop[1] | (loop[2] << 8) : -1;
    for (const limit of [240, 180, 120]) {
        const scale = Math.min(1, limit / width, limit / height);
        const targetWidth = Math.max(1, Math.round(width * scale));
        const targetHeight = Math.max(1, Math.round(height * scale));
        const result = await encodeAtSize(gif, frames, targetWidth, targetHeight, repeat, notify);
        if (result.size <= MAX_OUTPUT_BYTES) return result;
    }
    throw new Error("GIF sau khi thu nhỏ vẫn vượt 1 MB. Hãy chọn GIF ngắn hơn.");
}
