import { loadNativeBoundReadAddon, loadWindowsNativeAddon, stableNativePlatformKey } from "./native/loader.mjs";

export async function loadNativeAddon() {
  return loadNativeBoundReadAddon();
}

export { loadWindowsNativeAddon, stableNativePlatformKey };
