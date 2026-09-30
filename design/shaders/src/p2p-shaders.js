/*
  Punch2Pen shader runtime: Paper Shaders (Apache-2.0, https://github.com/paper-design/shaders)
  mounted without React, with brand presets built from design/tokens/tokens.json.

  Markup:
    <div data-p2p-shader="booth-glow"></div>
    <div data-p2p-shader="idle-smoke" data-image="../brand/fist-booth.svg"></div>
    <div data-p2p-shader="pad-card" data-p2p-params='{"seed": 7}'></div>

  Script:
    import { autoMount } from './p2p-shaders.js';
    autoMount();                       // every [data-p2p-shader] under document

  Rules the runtime enforces:
    - prefers-reduced-motion: every shader renders one frame and stops.
    - data-p2p-static on an element: same, regardless of motion settings.
    - No WebGL2: nothing mounts, the element keeps its CSS background (the fallback).
    - Paper's ShaderMount already pauses when the tab is hidden or the element is off screen.
*/
import {
  ShaderMount,
  ShaderFitOptions,
  getShaderColorFromString,
  getShaderNoiseTexture,
  grainGradientFragmentShader,
  GrainGradientShapes,
  paperTextureFragmentShader,
  gemSmokeFragmentShader,
  GemSmokeShapes,
  toProcessedGemSmoke,
  heatmapFragmentShader,
  toProcessedHeatmap,
  halftoneDotsFragmentShader,
  HalftoneDotsTypes,
  HalftoneDotsGrids,
  lensDistortionFragmentShader,
} from '@paper-design/shaders';
import { PRESETS } from './presets.js';

const SIZING = ['fit', 'scale', 'rotation', 'offsetX', 'offsetY', 'originX', 'originY', 'worldWidth', 'worldHeight'];
const OBJECT_SIZING = { fit: 'contain', scale: 1, rotation: 0, offsetX: 0, offsetY: 0, originX: 0.5, originY: 0.5, worldWidth: 0, worldHeight: 0 };

// One entry per shader we use. Enums map string params to the shader's integer constants.
const SHADERS = {
  grainGradient: { fragment: grainGradientFragmentShader, enums: { shape: GrainGradientShapes }, noise: true },
  paperTexture: { fragment: paperTextureFragmentShader, image: 'optional', isImageFlag: true, noise: true, mipmaps: ['u_image'] },
  gemSmoke: { fragment: gemSmokeFragmentShader, enums: { shape: GemSmokeShapes }, image: 'optional', isImageFlag: true, process: (url) => toProcessedGemSmoke(url).then((r) => r.pngBlob), mipmaps: ['u_image'] },
  heatmap: { fragment: heatmapFragmentShader, image: 'required', process: (url) => toProcessedHeatmap(url).then((r) => r.blob), mipmaps: ['u_image'] },
  halftoneDots: { fragment: halftoneDotsFragmentShader, enums: { type: HalftoneDotsTypes, grid: HalftoneDotsGrids }, image: 'optional' },
  lensDistortion: { fragment: lensDistortionFragmentShader, image: 'optional', mipmaps: ['u_image'] },
};

export { PRESETS };

export function webgl2Available() {
  try {
    return !!document.createElement('canvas').getContext('webgl2');
  } catch {
    return false;
  }
}

function prefersReducedMotion() {
  try {
    return window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  } catch {
    return false;
  }
}

function loaded(img) {
  if (img.complete && img.naturalWidth > 0) return Promise.resolve(img);
  return new Promise((resolve, reject) => {
    img.addEventListener('load', () => resolve(img), { once: true });
    img.addEventListener('error', () => reject(new Error(`p2p-shaders: could not load ${img.src.slice(0, 60)}`)), { once: true });
  });
}

function loadImage(src) {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => {
      // Paper's React wrapper upsizes small images so the shader samples them cleanly.
      if (img.naturalWidth < 1024 && img.naturalHeight < 1024 && img.naturalWidth > 0) {
        const aspect = img.naturalWidth / img.naturalHeight;
        img.width = Math.round(aspect > 1 ? 1024 * aspect : 1024);
        img.height = Math.round(aspect > 1 ? 1024 : 1024 / aspect);
      }
      resolve(img);
    };
    img.onerror = () => reject(new Error(`p2p-shaders: could not load image ${src}`));
    img.src = src;
  });
}

async function resolveImage(def, src) {
  if (!src) return undefined;
  if (!def.process) return loadImage(src);
  const blob = await def.process(src);
  return loadImage(URL.createObjectURL(blob));
}

function toUniforms(def, params, image) {
  const u = {};
  for (const [key, value] of Object.entries(params)) {
    if (key === 'speed' || key === 'frame' || key === 'image') continue;
    if (key === 'fit') u.u_fit = ShaderFitOptions[value];
    else if (key === 'colors') {
      u.u_colors = value.map(getShaderColorFromString);
      u.u_colorsCount = value.length;
    } else if (/^color[A-Z]/.test(key)) u[`u_${key}`] = getShaderColorFromString(value);
    else if (def.enums && def.enums[key]) u[`u_${key}`] = def.enums[key][value];
    else u[`u_${key}`] = value;
  }
  for (const key of SIZING) if (!(`u_${key}` in u) && key !== 'fit') u[`u_${key}`] = OBJECT_SIZING[key];
  if (!('u_fit' in u)) u.u_fit = ShaderFitOptions[OBJECT_SIZING.fit];
  if (def.image) u.u_image = image ?? emptyPixel();
  if (def.isImageFlag) u.u_isImage = Boolean(image);
  if (def.noise) u.u_noiseTexture = getShaderNoiseTexture();
  return u;
}

let _empty;
function emptyPixel() {
  if (!_empty) {
    _empty = new Image();
    _empty.src = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==';
  }
  return _empty;
}

/**
 * Mount one shader into an element.
 * @param {HTMLElement} el      the shader fills this element; give it a size
 * @param {string} presetName   a key of PRESETS
 * @param {object} overrides    optional params that win over the preset
 * @param {object} opts         { image, static, maxPixelCount, minPixelRatio }
 * @returns {Promise<ShaderMount|null>}  null when WebGL2 is unavailable
 */
export async function mountShader(el, presetName, overrides = {}, opts = {}) {
  const preset = PRESETS[presetName];
  if (!preset) throw new Error(`p2p-shaders: unknown preset "${presetName}"`);
  if (!webgl2Available()) {
    el.dataset.p2pShaderState = 'fallback';
    return null;
  }
  const def = SHADERS[preset.shader];
  const params = { ...preset.params, ...overrides };
  const imageSrc = opts.image ?? preset.image;
  if (def.image === 'required' && !imageSrc) throw new Error(`p2p-shaders: "${presetName}" needs an image`);
  const image = await resolveImage(def, imageSrc);
  const still = opts.static || prefersReducedMotion();
  const speed = still ? 0 : params.speed ?? 0;
  const uniforms = toUniforms(def, params, image);
  // ShaderMount rejects textures that are still loading (noise texture, empty pixel, logos).
  // Wait on the load event rather than img.decode(), which Chrome can reject for loaded images.
  await Promise.all(Object.values(uniforms).filter((v) => v instanceof HTMLImageElement).map(loaded));
  const mount = new ShaderMount(
    el,
    def.fragment,
    uniforms,
    undefined,
    speed,
    params.frame ?? 0,
    opts.minPixelRatio ?? preset.minPixelRatio ?? 2,
    opts.maxPixelCount ?? preset.maxPixelCount,
    def.mipmaps,
  );
  el.dataset.p2pShaderState = 'mounted';
  return mount;
}

/** Mount every [data-p2p-shader] under root. Returns the mounts in DOM order. */
export async function autoMount(root = document) {
  const nodes = [...root.querySelectorAll('[data-p2p-shader]')];
  return Promise.all(
    nodes.map((el) => {
      let overrides = {};
      try {
        overrides = el.dataset.p2pParams ? JSON.parse(el.dataset.p2pParams) : {};
      } catch {
        console.warn('p2p-shaders: bad data-p2p-params on', el);
      }
      return mountShader(el, el.dataset.p2pShader, overrides, {
        image: el.dataset.image,
        static: 'p2pStatic' in el.dataset,
      }).catch((err) => {
        el.dataset.p2pShaderState = 'error';
        console.warn(err);
        return null;
      });
    }),
  );
}
