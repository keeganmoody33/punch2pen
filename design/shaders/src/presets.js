/*
  Brand presets for Paper Shaders. Colors come from design/tokens/tokens.json
  (tokens.generated.js is written by ../build.mjs). REC red (#FF453A) never
  appears here. Brand red glows on site surfaces only: inside the plugin a red
  glow reads as armed or recording, so plugin shaders stay amber and white.

  Budget: one moving shader per screen. Plugin shaders only run while nothing is
  playing or recording, and stop the moment words arrive.
*/
import { T } from './tokens.generated.js';

const objectSizing = { fit: 'contain', scale: 1, rotation: 0, offsetX: 0, offsetY: 0, originX: 0.5, originY: 0.5, worldWidth: 0, worldHeight: 0 };
const patternSizing = { ...objectSizing, fit: 'none' };

export const PRESETS = {
  /* Site hero and section grounds: a red-lit booth through grain. Grain Gradient.
     The ramp stops at red-800 so white text on top keeps its contrast. */
  'booth-glow': {
    shader: 'grainGradient',
    where: 'Site hero ground, cover, section bands',
    params: {
      ...patternSizing,
      speed: 0.35,
      colorBack: T['graphite-900'],
      colors: [T['graphite-900'], T['red-950'], T['red-900'], T['red-800']],
      softness: 1,
      intensity: 0.22,
      noise: 0.32,
      shape: 'corners',
    },
  },

  /* The Pad as a real legal-pad sheet. Paper Texture 2.0 (Sep 2026): three-color
     scheme, wrinkles, roughness rows. Static: renders once, costs nothing after. */
  'pad-card': {
    shader: 'paperTexture',
    where: 'Tier cards, lyric sheet, share card',
    params: {
      ...objectSizing,
      fit: 'cover',
      speed: 0,
      colorBack: T['pad-shade'],
      colorPaper: T['pad-paper'],
      colorShadow: '#D6CB86',
      blending: 1,
      distortion: 0.2,
      clip: false,
      angle: 300,
      seed: 4,
      roughness: 0.28,
      roughnessSize: 0.35,
      roughnessRows: 0.35,
      fiber: 0.3,
      fiberSize: 0.4,
      folds: 0.18,
      foldSizeX: 1,
      foldSizeY: 1,
      foldOffsetX: 0,
      foldOffsetY: 0,
      wrinkles: 0.35,
      wrinkleSize: 0.5,
      crumples: 0,
      crumpleCount: 6,
      drops: 0.12,
    },
  },

  /* Plugin WAIT: the 2 smokes amber while the engine warms up. Gem Smoke (Apr 2026). */
  'wait-smoke': {
    shader: 'gemSmoke',
    where: 'Plugin disconnected empty state',
    image: '../brand/two-glyph-booth.svg',
    minPixelRatio: 1,
    maxPixelCount: 600 * 600,
    params: {
      ...objectSizing,
      scale: 0.72,
      speed: 0.45,
      colorBack: T['graphite-950'],
      colorInner: T['graphite-950'],
      colors: [T.wait, '#8C6B3C', T.ink],
      outerGlow: 0.35,
      innerGlow: 0.9,
      innerDistortion: 0.8,
      outerDistortion: 0.55,
      offset: 0,
      angle: 0,
      size: 0.8,
      shape: 'none',
    },
  },

  /* Plugin IDLE: the same 2 turns white once the engine answers. White, not red:
     a red glow in the plugin would read as recording. */
  'idle-smoke': {
    shader: 'gemSmoke',
    where: 'Plugin idle empty state',
    image: '../brand/two-glyph-booth.svg',
    minPixelRatio: 1,
    maxPixelCount: 600 * 600,
    params: {
      ...objectSizing,
      scale: 0.72,
      speed: 0.45,
      colorBack: T['graphite-950'],
      colorInner: T['graphite-950'],
      colors: [T['graphite-600'], '#B9BDC7', T.ink],
      outerGlow: 0.35,
      innerGlow: 0.9,
      innerDistortion: 0.8,
      outerDistortion: 0.55,
      offset: 0,
      angle: 0,
      size: 0.8,
      shape: 'none',
    },
  },

  /* The punch lands: the fist as a heat signature, brand red to white-hot. Heatmap. */
  'punch-heat': {
    shader: 'heatmap',
    where: 'Site brand band, cover, launch posts',
    image: '../brand/fist-booth.svg',
    params: {
      ...objectSizing,
      scale: 0.8,
      speed: 0.6,
      contour: 0.6,
      angle: 0,
      noise: 0.1,
      innerGlow: 0.75,
      outerGlow: 0.6,
      colorBack: T['graphite-900'],
      colors: [T['red-900'], T['red-700'], T['red-600'], T['red-300'], T.ink],
    },
  },

  /* Flyer print: marks and artist photos as black halftone on legal yellow, like a
     photocopied show flyer. Halftone Dots (Nov 2025). */
  'flyer-dots': {
    shader: 'halftoneDots',
    where: 'Share cards, creators page photos, merch',
    image: '../brand/fist-pad.svg',
    params: {
      ...objectSizing,
      fit: 'contain',
      scale: 0.9,
      speed: 0,
      colorBack: T['pad-paper'],
      colorFront: T['pad-ink'],
      size: 0.32,
      radius: 1.2,
      contrast: 0.55,
      originalColors: false,
      inverted: false,
      grainMixer: 0.15,
      grainOverlay: 0.15,
      grainSize: 0.5,
      grid: 'hex',
      type: 'gooey',
    },
  },

  /* 404: the wordmark through a lens. Lens Distortion (Aug 2026, newest). Static. */
  'lost-lens': {
    shader: 'lensDistortion',
    where: '404 page',
    image: '../brand/wordmark-booth.svg',
    params: {
      ...objectSizing,
      scale: 0.9,
      fit: 'contain',
      speed: 0,
      spread: 0.8,
      bias: 1,
      angle: 0,
      perspective: 0.1,
      count: 40,
      dispersion: 1,
      dispersionShift: 0,
      dispersionColor: 0.6,
      focusCenter: 0.8,
      focusEdges: 1,
      swirl: 0.5,
      noise: 0,
      noiseFrequency: 0.25,
      noiseOffset: 0,
      lensBulge: 0.3,
      lensCircle: 0,
      grainMixer: 0,
      grainOverlay: 0,
      imageX: 0,
      imageY: 0,
    },
  },
};
