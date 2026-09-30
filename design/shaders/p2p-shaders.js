/* Punch2Pen shader runtime. Bundles Paper Shaders 0.0.81 (Apache-2.0, https://github.com/paper-design/shaders). See design/shaders/NOTICE. */
var ye=`#version 300 es
precision mediump float;

layout(location = 0) in vec4 a_position;

uniform vec2 u_resolution;
uniform float u_pixelRatio;
uniform float u_imageAspectRatio;
uniform float u_originX;
uniform float u_originY;
uniform float u_worldWidth;
uniform float u_worldHeight;
uniform float u_fit;
uniform float u_scale;
uniform float u_rotation;
uniform float u_offsetX;
uniform float u_offsetY;

out vec2 v_objectUV;
out vec2 v_objectBoxSize;
out vec2 v_responsiveUV;
out vec2 v_responsiveBoxGivenSize;
out vec2 v_patternUV;
out vec2 v_patternBoxSize;
out vec2 v_imageUV;

vec3 getBoxSize(float boxRatio, vec2 givenBoxSize) {
  vec2 box = vec2(0.);
  // fit = none
  box.x = boxRatio * min(givenBoxSize.x / boxRatio, givenBoxSize.y);
  float noFitBoxWidth = box.x;
  if (u_fit == 1.) { // fit = contain
    box.x = boxRatio * min(u_resolution.x / boxRatio, u_resolution.y);
  } else if (u_fit == 2.) { // fit = cover
    box.x = boxRatio * max(u_resolution.x / boxRatio, u_resolution.y);
  }
  box.y = box.x / boxRatio;
  return vec3(box, noFitBoxWidth);
}

void main() {
  gl_Position = a_position;

  vec2 uv = gl_Position.xy * .5;
  vec2 boxOrigin = vec2(.5 - u_originX, u_originY - .5);
  vec2 givenBoxSize = vec2(u_worldWidth, u_worldHeight);
  givenBoxSize = max(givenBoxSize, vec2(1.)) * u_pixelRatio;
  float r = u_rotation * 3.14159265358979323846 / 180.;
  mat2 graphicRotation = mat2(cos(r), sin(r), -sin(r), cos(r));
  vec2 graphicOffset = vec2(-u_offsetX, u_offsetY);


  // ===================================================

  float fixedRatio = 1.;
  vec2 fixedRatioBoxGivenSize = vec2(
  (u_worldWidth == 0.) ? u_resolution.x : givenBoxSize.x,
  (u_worldHeight == 0.) ? u_resolution.y : givenBoxSize.y
  );

  v_objectBoxSize = getBoxSize(fixedRatio, fixedRatioBoxGivenSize).xy;
  vec2 objectWorldScale = u_resolution.xy / v_objectBoxSize;

  v_objectUV = uv;
  v_objectUV *= objectWorldScale;
  v_objectUV += boxOrigin * (objectWorldScale - 1.);
  v_objectUV += graphicOffset;
  v_objectUV /= u_scale;
  v_objectUV = graphicRotation * v_objectUV;

  // ===================================================

  v_responsiveBoxGivenSize = vec2(
  (u_worldWidth == 0.) ? u_resolution.x : givenBoxSize.x,
  (u_worldHeight == 0.) ? u_resolution.y : givenBoxSize.y
  );
  float responsiveRatio = v_responsiveBoxGivenSize.x / v_responsiveBoxGivenSize.y;
  vec2 responsiveBoxSize = getBoxSize(responsiveRatio, v_responsiveBoxGivenSize).xy;
  vec2 responsiveBoxScale = u_resolution.xy / responsiveBoxSize;

  #ifdef ADD_HELPERS
  v_responsiveHelperBox = uv;
  v_responsiveHelperBox *= responsiveBoxScale;
  v_responsiveHelperBox += boxOrigin * (responsiveBoxScale - 1.);
  #endif

  v_responsiveUV = uv;
  v_responsiveUV *= responsiveBoxScale;
  v_responsiveUV += boxOrigin * (responsiveBoxScale - 1.);
  v_responsiveUV += graphicOffset;
  v_responsiveUV /= u_scale;
  v_responsiveUV.x *= responsiveRatio;
  v_responsiveUV = graphicRotation * v_responsiveUV;
  v_responsiveUV.x /= responsiveRatio;

  // ===================================================

  float patternBoxRatio = givenBoxSize.x / givenBoxSize.y;
  vec2 patternBoxGivenSize = vec2(
  (u_worldWidth == 0.) ? u_resolution.x : givenBoxSize.x,
  (u_worldHeight == 0.) ? u_resolution.y : givenBoxSize.y
  );
  patternBoxRatio = patternBoxGivenSize.x / patternBoxGivenSize.y;

  vec3 boxSizeData = getBoxSize(patternBoxRatio, patternBoxGivenSize);
  v_patternBoxSize = boxSizeData.xy;
  float patternBoxNoFitBoxWidth = boxSizeData.z;
  vec2 patternBoxScale = u_resolution.xy / v_patternBoxSize;

  v_patternUV = uv;
  v_patternUV += graphicOffset / patternBoxScale;
  v_patternUV += boxOrigin;
  v_patternUV -= boxOrigin / patternBoxScale;
  v_patternUV *= u_resolution.xy;
  v_patternUV /= u_pixelRatio;
  if (u_fit > 0.) {
    v_patternUV *= (patternBoxNoFitBoxWidth / v_patternBoxSize.x);
  }
  v_patternUV /= u_scale;
  v_patternUV = graphicRotation * v_patternUV;
  v_patternUV += boxOrigin / patternBoxScale;
  v_patternUV -= boxOrigin;
  // x100 is a default multiplier between vertex and fragmant shaders
  // we use it to avoid UV presision issues
  v_patternUV *= .01;

  // ===================================================

  vec2 imageBoxSize;
  if (u_fit == 1.) { // contain
    imageBoxSize.x = min(u_resolution.x / u_imageAspectRatio, u_resolution.y) * u_imageAspectRatio;
  } else if (u_fit == 2.) { // cover
    imageBoxSize.x = max(u_resolution.x / u_imageAspectRatio, u_resolution.y) * u_imageAspectRatio;
  } else {
    imageBoxSize.x = min(10.0, 10.0 / u_imageAspectRatio * u_imageAspectRatio);
  }
  imageBoxSize.y = imageBoxSize.x / u_imageAspectRatio;
  vec2 imageBoxScale = u_resolution.xy / imageBoxSize;

  v_imageUV = uv;
  v_imageUV *= imageBoxScale;
  v_imageUV += boxOrigin * (imageBoxScale - 1.);
  v_imageUV += graphicOffset;
  v_imageUV /= u_scale;
  v_imageUV.x *= u_imageAspectRatio;
  v_imageUV = graphicRotation * v_imageUV;
  v_imageUV.x /= u_imageAspectRatio;

  v_imageUV += .5;
  v_imageUV.y = 1. - v_imageUV.y;
}`;var Ee=1920*1080*4,Y=class{parentElement;canvasElement;gl;program=null;uniformLocations={};fragmentShader;rafId=null;lastRenderTime=0;currentFrame=0;speed=0;currentSpeed=0;providedUniforms;mipmaps=[];hasBeenDisposed=!1;resolutionChanged=!0;textures=new Map;minPixelRatio;maxPixelCount;isSafari=We();uniformCache={};textureUnitMap=new Map;ownerDocument;constructor(e,a,o,i,s=0,n=0,r=2,m=Ee,l=[]){if(e?.nodeType===1)this.parentElement=e;else throw new Error("Paper Shaders: parent element must be an HTMLElement");if(this.ownerDocument=e.ownerDocument,!this.ownerDocument.querySelector("style[data-paper-shader]")){let d=this.ownerDocument.createElement("style");d.innerHTML=ze,d.setAttribute("data-paper-shader",""),this.ownerDocument.head.prepend(d)}let p=this.ownerDocument.createElement("canvas");this.canvasElement=p,this.parentElement.prepend(p),this.fragmentShader=a,this.providedUniforms=o,this.mipmaps=l,this.currentFrame=n,this.minPixelRatio=r,this.maxPixelCount=m;let f=p.getContext("webgl2",i);if(!f)throw new Error("Paper Shaders: WebGL is not supported in this browser");this.gl=f,this.initProgram(),this.setupPositionAttribute(),this.setupUniforms(),this.setUniformValues(this.providedUniforms),this.setupResizeObserver(),visualViewport?.addEventListener("resize",this.handleVisualViewportChange),this.setupIntersectionObserver(),this.setSpeed(s),this.parentElement.setAttribute("data-paper-shader",""),this.parentElement.paperShaderMount=this,this.ownerDocument.addEventListener("visibilitychange",this.handleDocumentVisibilityChange)}initProgram=()=>{let e=Oe(this.gl,ye,this.fragmentShader);e&&(this.program=e)};setupPositionAttribute=()=>{let e=this.gl.getAttribLocation(this.program,"a_position"),a=this.gl.createBuffer();this.gl.bindBuffer(this.gl.ARRAY_BUFFER,a);let o=[-1,-1,1,-1,-1,1,-1,1,1,-1,1,1];this.gl.bufferData(this.gl.ARRAY_BUFFER,new Float32Array(o),this.gl.STATIC_DRAW),this.gl.enableVertexAttribArray(e),this.gl.vertexAttribPointer(e,2,this.gl.FLOAT,!1,0,0)};setupUniforms=()=>{let e={u_time:this.gl.getUniformLocation(this.program,"u_time"),u_pixelRatio:this.gl.getUniformLocation(this.program,"u_pixelRatio"),u_resolution:this.gl.getUniformLocation(this.program,"u_resolution")};Object.entries(this.providedUniforms).forEach(([a,o])=>{if(e[a]=this.gl.getUniformLocation(this.program,a),o instanceof HTMLImageElement){let i=`${a}AspectRatio`;e[i]=this.gl.getUniformLocation(this.program,i)}}),this.uniformLocations=e};renderScale=1;parentWidth=0;parentHeight=0;parentDevicePixelWidth=0;parentDevicePixelHeight=0;devicePixelsSupported=!1;intersectionObserver=null;isInViewport=!0;resizeObserver=null;setupResizeObserver=()=>{this.resizeObserver=new ResizeObserver(([e])=>{if(e?.borderBoxSize[0]){let a=e.devicePixelContentBoxSize?.[0];a!==void 0&&(this.devicePixelsSupported=!0,this.parentDevicePixelWidth=a.inlineSize,this.parentDevicePixelHeight=a.blockSize),this.parentWidth=e.borderBoxSize[0].inlineSize,this.parentHeight=e.borderBoxSize[0].blockSize}this.handleResize()}),this.resizeObserver.observe(this.parentElement)};setupIntersectionObserver=()=>{let e=this.ownerDocument.defaultView;e?.IntersectionObserver&&(this.intersectionObserver=new e.IntersectionObserver(([a])=>{this.isInViewport=a?.isIntersecting??!0,this.updateCurrentSpeed()}),this.intersectionObserver.observe(this.parentElement))};handleVisualViewportChange=()=>{this.resizeObserver?.disconnect(),this.setupResizeObserver()};handleResize=()=>{let e=0,a=0,o=Math.max(1,window.devicePixelRatio),i=visualViewport?.scale??1;if(this.devicePixelsSupported){let p=Math.max(1,this.minPixelRatio/o);e=this.parentDevicePixelWidth*p*i,a=this.parentDevicePixelHeight*p*i}else{let p=Math.max(o,this.minPixelRatio)*i;if(this.isSafari){let f=Le(this.ownerDocument);p*=Math.max(1,f)}e=Math.round(this.parentWidth)*p,a=Math.round(this.parentHeight)*p}let s=Math.sqrt(this.maxPixelCount)/Math.sqrt(e*a),n=Math.min(1,s),r=Math.round(e*n),m=Math.round(a*n),l=r/Math.round(this.parentWidth);(this.canvasElement.width!==r||this.canvasElement.height!==m||this.renderScale!==l)&&(this.renderScale=l,this.canvasElement.width=r,this.canvasElement.height=m,this.resolutionChanged=!0,this.gl.viewport(0,0,this.gl.canvas.width,this.gl.canvas.height),this.render(performance.now()))};render=e=>{if(this.hasBeenDisposed)return;if(this.program===null){console.warn("Tried to render before program or gl was initialized");return}let a=e-this.lastRenderTime;this.lastRenderTime=e,this.currentSpeed!==0&&(this.currentFrame+=a*this.currentSpeed),this.gl.clear(this.gl.COLOR_BUFFER_BIT),this.gl.useProgram(this.program),this.gl.uniform1f(this.uniformLocations.u_time,this.currentFrame*.001),this.resolutionChanged&&(this.gl.uniform2f(this.uniformLocations.u_resolution,this.gl.canvas.width,this.gl.canvas.height),this.gl.uniform1f(this.uniformLocations.u_pixelRatio,this.renderScale),this.resolutionChanged=!1),this.gl.drawArrays(this.gl.TRIANGLES,0,6),this.currentSpeed!==0?this.requestRender():this.rafId=null};requestRender=()=>{this.rafId!==null&&cancelAnimationFrame(this.rafId),this.rafId=requestAnimationFrame(this.render)};setTextureUniform=(e,a)=>{if(!a.complete||a.naturalWidth===0)throw new Error(`Paper Shaders: image for uniform ${e} must be fully loaded`);let o=this.textures.get(e);o&&this.gl.deleteTexture(o),this.textureUnitMap.has(e)||this.textureUnitMap.set(e,this.textureUnitMap.size);let i=this.textureUnitMap.get(e);this.gl.activeTexture(this.gl.TEXTURE0+i);let s=this.gl.createTexture();this.gl.bindTexture(this.gl.TEXTURE_2D,s),this.gl.texParameteri(this.gl.TEXTURE_2D,this.gl.TEXTURE_WRAP_S,this.gl.CLAMP_TO_EDGE),this.gl.texParameteri(this.gl.TEXTURE_2D,this.gl.TEXTURE_WRAP_T,this.gl.CLAMP_TO_EDGE),this.gl.texParameteri(this.gl.TEXTURE_2D,this.gl.TEXTURE_MIN_FILTER,this.gl.LINEAR),this.gl.texParameteri(this.gl.TEXTURE_2D,this.gl.TEXTURE_MAG_FILTER,this.gl.LINEAR),this.gl.texImage2D(this.gl.TEXTURE_2D,0,this.gl.RGBA,this.gl.RGBA,this.gl.UNSIGNED_BYTE,a),this.mipmaps.includes(e)&&(this.gl.generateMipmap(this.gl.TEXTURE_2D),this.gl.texParameteri(this.gl.TEXTURE_2D,this.gl.TEXTURE_MIN_FILTER,this.gl.LINEAR_MIPMAP_LINEAR));let n=this.gl.getError();if(n!==this.gl.NO_ERROR||s===null){console.error("Paper Shaders: WebGL error when uploading texture:",n);return}this.textures.set(e,s);let r=this.uniformLocations[e];if(r){this.gl.uniform1i(r,i);let m=`${e}AspectRatio`,l=this.uniformLocations[m];if(l){let p=a.naturalWidth/a.naturalHeight;this.gl.uniform1f(l,p)}}};areUniformValuesEqual=(e,a)=>e===a?!0:Array.isArray(e)&&Array.isArray(a)&&e.length===a.length?e.every((o,i)=>this.areUniformValuesEqual(o,a[i])):!1;setUniformValues=e=>{this.gl.useProgram(this.program),Object.entries(e).forEach(([a,o])=>{let i=o;if(o instanceof HTMLImageElement&&(i=`${o.src.slice(0,200)}|${o.naturalWidth}x${o.naturalHeight}`),this.areUniformValuesEqual(this.uniformCache[a],i))return;this.uniformCache[a]=i;let s=this.uniformLocations[a];if(!s){console.warn(`Uniform location for ${a} not found`);return}if(o instanceof HTMLImageElement)this.setTextureUniform(a,o);else if(Array.isArray(o)){let n=null,r=null;if(o[0]!==void 0&&Array.isArray(o[0])){let m=o[0].length;if(o.every(l=>l.length===m))n=o.flat(),r=m;else{console.warn(`All child arrays must be the same length for ${a}`);return}}else n=o,r=n.length;switch(r){case 2:this.gl.uniform2fv(s,n);break;case 3:this.gl.uniform3fv(s,n);break;case 4:this.gl.uniform4fv(s,n);break;case 9:this.gl.uniformMatrix3fv(s,!1,n);break;case 16:this.gl.uniformMatrix4fv(s,!1,n);break;default:console.warn(`Unsupported uniform array length: ${r}`)}}else typeof o=="number"?this.gl.uniform1f(s,o):typeof o=="boolean"?this.gl.uniform1i(s,o?1:0):console.warn(`Unsupported uniform type for ${a}: ${typeof o}`)})};getCurrentFrame=()=>this.currentFrame;setFrame=e=>{this.currentFrame=e,this.lastRenderTime=performance.now(),this.render(performance.now())};setSpeed=(e=1)=>{this.speed=e,this.updateCurrentSpeed()};updateCurrentSpeed=()=>{this.setCurrentSpeed(this.ownerDocument.hidden||!this.isInViewport?0:this.speed)};setCurrentSpeed=e=>{this.currentSpeed=e,this.rafId===null&&e!==0&&(this.lastRenderTime=performance.now(),this.rafId=requestAnimationFrame(this.render)),this.rafId!==null&&e===0&&(cancelAnimationFrame(this.rafId),this.rafId=null)};setMaxPixelCount=(e=Ee)=>{this.maxPixelCount=e,this.handleResize()};setMinPixelRatio=(e=2)=>{this.minPixelRatio=e,this.handleResize()};setUniforms=e=>{this.setUniformValues(e),this.providedUniforms={...this.providedUniforms,...e},this.render(performance.now())};handleDocumentVisibilityChange=()=>{this.updateCurrentSpeed()};dispose=()=>{this.hasBeenDisposed=!0,this.rafId!==null&&(cancelAnimationFrame(this.rafId),this.rafId=null),this.gl&&this.program&&(this.textures.forEach(e=>{this.gl.deleteTexture(e)}),this.textures.clear(),this.gl.deleteProgram(this.program),this.program=null,this.gl.bindBuffer(this.gl.ARRAY_BUFFER,null),this.gl.bindBuffer(this.gl.ELEMENT_ARRAY_BUFFER,null),this.gl.bindRenderbuffer(this.gl.RENDERBUFFER,null),this.gl.bindFramebuffer(this.gl.FRAMEBUFFER,null),this.gl.getError()),this.resizeObserver&&(this.resizeObserver.disconnect(),this.resizeObserver=null),this.intersectionObserver&&(this.intersectionObserver.disconnect(),this.intersectionObserver=null),visualViewport?.removeEventListener("resize",this.handleVisualViewportChange),this.ownerDocument.removeEventListener("visibilitychange",this.handleDocumentVisibilityChange),this.uniformLocations={},this.canvasElement.remove(),delete this.parentElement.paperShaderMount}};function Ue(t,e,a){let o=t.createShader(e);return o?(t.shaderSource(o,a),t.compileShader(o),t.getShaderParameter(o,t.COMPILE_STATUS)?o:(console.error("An error occurred compiling the shaders: "+t.getShaderInfoLog(o)),t.deleteShader(o),null)):null}function Oe(t,e,a){let o=t.getShaderPrecisionFormat(t.FRAGMENT_SHADER,t.MEDIUM_FLOAT),i=o?o.precision:null;i&&i<23&&(e=e.replace(/precision\s+(lowp|mediump)\s+float;/g,"precision highp float;"),a=a.replace(/precision\s+(lowp|mediump)\s+float/g,"precision highp float").replace(/\b(uniform|varying|attribute)\s+(lowp|mediump)\s+(\w+)/g,"$1 highp $3"));let s=Ue(t,t.VERTEX_SHADER,e),n=Ue(t,t.FRAGMENT_SHADER,a);if(!s||!n)return null;let r=t.createProgram();return r?(t.attachShader(r,s),t.attachShader(r,n),t.linkProgram(r),t.getProgramParameter(r,t.LINK_STATUS)?(t.detachShader(r,s),t.detachShader(r,n),t.deleteShader(s),t.deleteShader(n),r):(console.error("Unable to initialize the shader program: "+t.getProgramInfoLog(r)),t.deleteProgram(r),t.deleteShader(s),t.deleteShader(n),null)):null}var ze=`@layer paper-shaders {
  :where([data-paper-shader]) {
    isolation: isolate;
    position: relative;

    & canvas {
      contain: strict;
      display: block;
      position: absolute;
      inset: 0;
      z-index: -1;
      width: 100%;
      height: 100%;
      border-radius: inherit;
      corner-shape: inherit;
    }
  }
}`;function We(){let t=navigator.userAgent.toLowerCase();return t.includes("safari")&&!t.includes("chrome")&&!t.includes("android")}function Le(t){let e=visualViewport?.scale??1,a=visualViewport?.width??window.innerWidth,o=window.innerWidth-t.documentElement.clientWidth,i=e*a+o,s=outerWidth/i,n=Math.round(100*s);return n%5===0?n/100:n===33?1/3:n===67?2/3:n===133?4/3:s}var q={none:0,contain:1,cover:2};var M=`
#define TWO_PI 6.28318530718
#define PI 3.14159265358979323846
`,Q=`
vec2 rotate(vec2 uv, float th) {
  return mat2(cos(th), sin(th), -sin(th), cos(th)) * uv;
}
`,_e=`
  float hash11(float p) {
    p = fract(p * 0.3183099) + 0.1;
    p *= p + 19.19;
    return fract(p * p);
  }
`,O=`
  float hash21(vec2 p) {
    p = fract(p * vec2(0.3183099, 0.3678794)) + 0.1;
    p += dot(p, p + 19.19);
    return fract(p.x * p.y);
  }
`,Fe=`
  vec2 hash22(vec2 p) {
    p = fract(p * vec2(0.3183099, 0.3678794)) + 0.1;
    p += dot(p, p.yx + 19.19);
    return fract(vec2(p.x * p.y, p.x + p.y));
  }
`,De=`
  float randomR(vec2 p) {
    vec2 uv = floor(p) / 100. + .5;
    return texture(u_noiseTexture, fract(uv)).r;
  }
`;var Ie=`
vec3 permute(vec3 x) { return mod(((x * 34.0) + 1.0) * x, 289.0); }
float snoise(vec2 v) {
  const vec4 C = vec4(0.211324865405187, 0.366025403784439,
    -0.577350269189626, 0.024390243902439);
  vec2 i = floor(v + dot(v, C.yy));
  vec2 x0 = v - i + dot(i, C.xx);
  vec2 i1;
  i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
  vec4 x12 = x0.xyxy + C.xxzz;
  x12.xy -= i1;
  i = mod(i, 289.0);
  vec3 p = permute(permute(i.y + vec3(0.0, i1.y, 1.0))
    + i.x + vec3(0.0, i1.x, 1.0));
  vec3 m = max(0.5 - vec3(dot(x0, x0), dot(x12.xy, x12.xy),
      dot(x12.zw, x12.zw)), 0.0);
  m = m * m;
  m = m * m;
  vec3 x = 2.0 * fract(p * C.www) - 1.0;
  vec3 h = abs(x) - 0.5;
  vec3 ox = floor(x + 0.5);
  vec3 a0 = x - ox;
  m *= 1.79284291400159 - 0.85373472095314 * (a0 * a0 + h * h);
  vec3 g;
  g.x = a0.x * x0.x + h.x * x0.y;
  g.yz = a0.yz * x12.xz + h.yz * x12.yw;
  return 130.0 * dot(m, g);
}
`;var ae={maxColorCount:7},ie=`#version 300 es
precision lowp float;

uniform mediump float u_time;
uniform mediump vec2 u_resolution;
uniform mediump float u_pixelRatio;

uniform sampler2D u_noiseTexture;

uniform vec4 u_colorBack;
uniform vec4 u_colors[${ae.maxColorCount}];
uniform float u_colorsCount;
uniform float u_softness;
uniform float u_intensity;
uniform float u_noise;
uniform float u_shape;

uniform mediump float u_worldWidth;
uniform mediump float u_worldHeight;
uniform mediump float u_fit;

uniform mediump float u_scale;
uniform mediump float u_rotation;
uniform mediump float u_offsetX;
uniform mediump float u_offsetY;

in vec2 v_objectUV;
in vec2 v_patternUV;
in vec2 v_objectBoxSize;
in vec2 v_patternBoxSize;

out vec4 fragColor;

${M}
${Ie}
${Q}
${De}

float valueNoiseR(vec2 st) {
  vec2 i = floor(st);
  vec2 f = fract(st);
  float a = randomR(i);
  float b = randomR(i + vec2(1.0, 0.0));
  float c = randomR(i + vec2(0.0, 1.0));
  float d = randomR(i + vec2(1.0, 1.0));
  vec2 u = f * f * (3.0 - 2.0 * f);
  float x1 = mix(a, b, u.x);
  float x2 = mix(c, d, u.x);
  return mix(x1, x2, u.y);
}
vec4 fbmR(vec2 n0, vec2 n1, vec2 n2, vec2 n3) {
  float amplitude = 0.2;
  vec4 total = vec4(0.);
  for (int i = 0; i < 3; i++) {
    n0 = rotate(n0, 0.3);
    n1 = rotate(n1, 0.3);
    n2 = rotate(n2, 0.3);
    n3 = rotate(n3, 0.3);
    total.x += valueNoiseR(n0) * amplitude;
    total.y += valueNoiseR(n1) * amplitude;
    total.z += valueNoiseR(n2) * amplitude;
    total.z += valueNoiseR(n3) * amplitude;
    n0 *= 1.99;
    n1 *= 1.99;
    n2 *= 1.99;
    n3 *= 1.99;
    amplitude *= 0.6;
  }
  return total;
}

${_e}

vec2 truchet(vec2 uv, float idx){
  idx = fract(((idx - .5) * 2.));
  if (idx > 0.75) {
    uv = vec2(1.0) - uv;
  } else if (idx > 0.5) {
    uv = vec2(1.0 - uv.x, uv.y);
  } else if (idx > 0.25) {
    uv = 1.0 - vec2(1.0 - uv.x, uv.y);
  }
  return uv;
}

void main() {

  const float firstFrameOffset = 7.;
  float t = .1 * (u_time + firstFrameOffset);

  vec2 shape_uv = vec2(0.);
  vec2 grain_uv = vec2(0.);

  float r = u_rotation * PI / 180.;
  float cr = cos(r);
  float sr = sin(r);
  mat2 graphicRotation = mat2(cr, sr, -sr, cr);
  vec2 graphicOffset = vec2(-u_offsetX, u_offsetY);

  if (u_shape > 3.5) {
    shape_uv = v_objectUV;
    grain_uv = shape_uv;

    // apply inverse transform to grain_uv so it respects the originXY
    grain_uv = transpose(graphicRotation) * grain_uv;
    grain_uv *= u_scale;
    grain_uv -= graphicOffset;
    grain_uv *= v_objectBoxSize;
    grain_uv *= .7;
  } else {
    shape_uv = .5 * v_patternUV;
    grain_uv = 100. * v_patternUV;

    // apply inverse transform to grain_uv so it respects the originXY
    grain_uv = transpose(graphicRotation) * grain_uv;
    grain_uv *= u_scale;
    if (u_fit > 0.) {
      vec2 givenBoxSize = vec2(u_worldWidth, u_worldHeight);
      givenBoxSize = max(givenBoxSize, vec2(1.)) * u_pixelRatio;
      float patternBoxRatio = givenBoxSize.x / givenBoxSize.y;
      vec2 patternBoxGivenSize = vec2(
      (u_worldWidth == 0.) ? u_resolution.x : givenBoxSize.x,
      (u_worldHeight == 0.) ? u_resolution.y : givenBoxSize.y
      );
      patternBoxRatio = patternBoxGivenSize.x / patternBoxGivenSize.y;
      float patternBoxNoFitBoxWidth = patternBoxRatio * min(patternBoxGivenSize.x / patternBoxRatio, patternBoxGivenSize.y);
      grain_uv /= (patternBoxNoFitBoxWidth / v_patternBoxSize.x);
    }
    vec2 patternBoxScale = u_resolution.xy / v_patternBoxSize;
    grain_uv -= graphicOffset / patternBoxScale;
    grain_uv *= 1.6;
  }


  float shape = 0.;

  if (u_shape < 1.5) {
    // Sine wave

    float wave = cos(.5 * shape_uv.x - 4. * t) * sin(1.5 * shape_uv.x + 2. * t) * (.75 + .25 * cos(6. * t));
    shape = 1. - smoothstep(-1., 1., shape_uv.y + wave);

  } else if (u_shape < 2.5) {
    // Grid (dots)

    float stripeIdx = floor(2. * shape_uv.x / TWO_PI);
    float rand = hash11(stripeIdx * 100.);
    rand = sign(rand - .5) * pow(4. * abs(rand), .3);
    shape = sin(shape_uv.x) * cos(shape_uv.y - 5. * rand * t);
    shape = pow(abs(shape), 4.);

  } else if (u_shape < 3.5) {
    // Truchet pattern

    float n2 = valueNoiseR(shape_uv * .4 - 3.75 * t);
    shape_uv.x += 10.;
    shape_uv *= .6;

    vec2 tile = truchet(fract(shape_uv), randomR(floor(shape_uv)));

    float distance1 = length(tile);
    float distance2 = length(tile - vec2(1.));

    n2 -= .5;
    n2 *= .1;
    shape = smoothstep(.2, .55, distance1 + n2) * (1. - smoothstep(.45, .8, distance1 - n2));
    shape += smoothstep(.2, .55, distance2 + n2) * (1. - smoothstep(.45, .8, distance2 - n2));

    shape = pow(shape, 1.5);

  } else if (u_shape < 4.5) {
    // Corners

    shape_uv *= .6;
    vec2 outer = vec2(.5);

    vec2 bl = smoothstep(vec2(0.), outer, shape_uv + vec2(.1 + .1 * sin(3. * t), .2 - .1 * sin(5.25 * t)));
    vec2 tr = smoothstep(vec2(0.), outer, 1. - shape_uv);
    shape = 1. - bl.x * bl.y * tr.x * tr.y;

    shape_uv = -shape_uv;
    bl = smoothstep(vec2(0.), outer, shape_uv + vec2(.1 + .1 * sin(3. * t), .2 - .1 * cos(5.25 * t)));
    tr = smoothstep(vec2(0.), outer, 1. - shape_uv);
    shape -= bl.x * bl.y * tr.x * tr.y;

    shape = 1. - smoothstep(0., 1., shape);

  } else if (u_shape < 5.5) {
    // Ripple

    shape_uv *= 2.;
    float dist = length(.4 * shape_uv);
    float waves = sin(pow(dist, 1.2) * 5. - 3. * t) * .5 + .5;
    shape = waves;

  } else if (u_shape < 6.5) {
    // Blob

    t *= 2.;

    vec2 f1_traj = .25 * vec2(1.3 * sin(t), .2 + 1.3 * cos(.6 * t + 4.));
    vec2 f2_traj = .2 * vec2(1.2 * sin(-t), 1.3 * sin(1.6 * t));
    vec2 f3_traj = .25 * vec2(1.7 * cos(-.6 * t), cos(-1.6 * t));
    vec2 f4_traj = .3 * vec2(1.4 * cos(.8 * t), 1.2 * sin(-.6 * t - 3.));

    shape = .5 * pow(1. - clamp(0., 1., length(shape_uv + f1_traj)), 5.);
    shape += .5 * pow(1. - clamp(0., 1., length(shape_uv + f2_traj)), 5.);
    shape += .5 * pow(1. - clamp(0., 1., length(shape_uv + f3_traj)), 5.);
    shape += .5 * pow(1. - clamp(0., 1., length(shape_uv + f4_traj)), 5.);

    shape = smoothstep(.0, .9, shape);
    float edge = smoothstep(.25, .3, shape);
    shape = mix(.0, shape, edge);

  } else {
    // Sphere

    shape_uv *= 2.;
    float d = 1. - pow(length(shape_uv), 2.);
    vec3 pos = vec3(shape_uv, sqrt(max(d, 0.)));
    vec3 lightPos = normalize(vec3(cos(1.5 * t), .8, sin(1.25 * t)));
    shape = .5 + .5 * dot(lightPos, pos);
    shape *= step(0., d);
  }

  float baseNoise = snoise(grain_uv * .5);
  vec4 fbmVals = fbmR(
  .002 * grain_uv + 10.,
  .003 * grain_uv,
  .001 * grain_uv,
  rotate(.4 * grain_uv, 2.)
  );
  float grainDist = baseNoise * snoise(grain_uv * .2) - fbmVals.x - fbmVals.y;
  float rawNoise = .75 * baseNoise - fbmVals.w - fbmVals.z;
  float noise = clamp(rawNoise, 0., 1.);

  shape += u_intensity * 2. / u_colorsCount * (grainDist + .5);
  shape += u_noise * 10. / u_colorsCount * noise;

  float aa = fwidth(shape);

  shape = clamp(shape - .5 / u_colorsCount, 0., 1.);
  float totalShape = smoothstep(0., u_softness + 2. * aa, clamp(shape * u_colorsCount, 0., 1.));
  float mixer = shape * (u_colorsCount - 1.);

  int cntStop = int(u_colorsCount) - 1;
  vec4 gradient = u_colors[0];
  gradient.rgb *= gradient.a;
  for (int i = 1; i < ${ae.maxColorCount}; i++) {
    if (i > cntStop) break;

    float localT = clamp(mixer - float(i - 1), 0., 1.);
    localT = smoothstep(.5 - .5 * u_softness - aa, .5 + .5 * u_softness + aa, localT);

    vec4 c = u_colors[i];
    c.rgb *= c.a;
    gradient = mix(gradient, c, localT);
  }

  vec3 color = gradient.rgb * totalShape;
  float opacity = gradient.a * totalShape;

  vec3 bgColor = u_colorBack.rgb * u_colorBack.a;
  color = color + bgColor * (1.0 - opacity);
  opacity = opacity + u_colorBack.a * (1.0 - opacity);

  fragColor = vec4(color, opacity);
}
`,re={wave:1,dots:2,truchet:3,corners:4,ripple:5,blob:6,sphere:7};var J={maxCrumpleCount:15},se=`#version 300 es
precision mediump float;

uniform sampler2D u_image;
uniform bool u_isImage;
uniform float u_imageAspectRatio;

uniform vec4 u_colorBack;
uniform vec4 u_colorPaper;
uniform vec4 u_colorShadow;

uniform float u_blending;
uniform float u_distortion;
uniform bool u_clip;
uniform float u_angle;
uniform float u_seed;

uniform float u_roughness;
uniform float u_roughnessSize;
uniform float u_roughnessRows;
uniform float u_fiber;
uniform float u_fiberSize;
uniform float u_folds;
uniform float u_foldSizeX;
uniform float u_foldSizeY;
uniform float u_foldOffsetX;
uniform float u_foldOffsetY;
uniform float u_wrinkles;
uniform float u_wrinkleSize;
uniform float u_crumples;
uniform float u_crumpleCount;
uniform float u_drops;

uniform sampler2D u_noiseTexture;

in vec2 v_imageUV;
out vec4 fragColor;

float getUvFrame(vec2 uv) {
  vec2 invAA = .5 / clamp(fwidth(uv), 1e-5, .02);
  vec2 lo = clamp(uv * invAA + .5, 0., 1.);
  vec2 hi = clamp((1. - uv) * invAA + .5, 0., 1.);
  return lo.x * hi.x * lo.y * hi.y;
}

float lst(float edge0, float edge1, float x) {
  return clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
}

${M}
${O}
${Fe}

float getRoughness(vec2 p, vec2 lightDir, vec2 seedShift, float basePixel) {
  vec2 u = p / vec2(2, 4);
  float w = 100. * basePixel;
  float eps = 4. * w;

  float size = mix(3.2, .8, u_roughnessSize);
  float logLac = log2(2.1);
  float level = -log2(w + 1e-8) / logLac;
  float baseLevel = floor(level) - 2.;
  float fade = fract(level);

  vec2 px = (u.x + vec2(eps, -eps)) * .1;
  float py = u.y * .1;

  vec2 sum = vec2(0.);
  float norm = 0., amp = .5;
  float freq = exp2(baseLevel * logLac);
  for (int i = 0; i < 4; i++) {
    float absIdx = baseLevel + float(i);
    vec2 qx = size * px * freq;
    float qy = size * py * freq;

    float wi = 1.;
    if (i == 0) wi = 1. - fade;
    if (i == 3) wi = fade;

    vec2 fx = fract(qx);
    float fy = fract(qy);
    vec2 shift = .5 + absIdx * .3 + seedShift;
    float uvY = floor(qy) / 50. + shift.y;
    vec2 s0a = textureLod(u_noiseTexture, fract(vec2(floor(qx.x) / 50. + shift.x, uvY)), 0.).rg;
    vec2 s1a = textureLod(u_noiseTexture, fract(vec2( ceil(qx.x) / 50. + shift.x, uvY)), 0.).rg;
    vec2 s0b = textureLod(u_noiseTexture, fract(vec2(floor(qx.y) / 50. + shift.x, uvY)), 0.).rg;
    vec2 s1b = textureLod(u_noiseTexture, fract(vec2( ceil(qx.y) / 50. + shift.x, uvY)), 0.).rg;
    vec2 ny0 = mix(vec2(s0a.r, s0b.r), vec2(s0a.g, s0b.g), fy);
    vec2 ny1 = mix(vec2(s1a.r, s1b.r), vec2(s1a.g, s1b.g), fy);
    vec2 n = mix(ny0, ny1, fx);

    sum += amp * wi * n;
    norm += amp * wi;
    amp *= .8;
    freq *= 2.1;
  }

  vec2 r = sum / norm;
  float dx = .5 + r.x - r.y;
  float grain = 3. * dx * dx - .7;

  float rowBand = fract(dot(p, lightDir) * .05 * size);
  return grain + .6 * u_roughnessRows * (rowBand - .5);
}

float getFiber(vec2 p, vec2 seedShift, float basePixel) {
  float size = mix(4., 1., u_fiberSize);
  float w = 50. * basePixel;
  float level = -log2(w + 1e-8);
  float baseLevel = floor(level) - 3.;
  float fade = fract(level);

  vec2 grad = vec2(0.);
  float scale = 1.;
  float amp = 1.;
  float freq = pow(1.7, baseLevel);
  for (int i = 0; i < 5; i++) {
    float absIdx = baseLevel + float(i);
    vec2 q = size * p * freq;

    float an = absIdx * .8;
    float rc = cos(an), rs = sin(an);
    q = vec2(rc * q.x - rs * q.y, rs * q.x + rc * q.y);

    float wi = 1.;
    if (i == 0) wi = 1. - fade;
    if (i == 4) wi = fade;

    vec2 iq = floor(q);
    vec2 fq = fract(q);
    float shift = absIdx * .3;
    vec4 uv = fract(vec4(iq, iq + 1.) / 50. + .5 + shift + seedShift.xyxy);
    float aF = textureLod(u_noiseTexture, uv.xy, 0.).b;
    float bF = textureLod(u_noiseTexture, uv.zy, 0.).b;
    float cF = textureLod(u_noiseTexture, uv.xw, 0.).b;
    float dF = textureLod(u_noiseTexture, uv.zw, 0.).b;
    vec2 u = fq * fq * (3. - 2. * fq);
    vec2 du = 8. * fq * (1. - fq);
    float dx = du.x * mix(bF - aF, dF - cF, u.y);
    float dy = du.y * mix(cF - aF, dF - bF, u.x);
    grad += wi * amp * scale * vec2(rc * dx + rs * dy, -rs * dx + rc * dy);
    scale *= 1.7;
    amp *= .5;
    freq *= 1.7;
  }

  return clamp(.333 * length(grad), 0., 1.);
}

vec2 smoothNoise(vec2 p) {
  vec2 t = p * 50. - .5;
  vec2 i = floor(t);
  vec2 f = fract(t);
  f = f * f * (3. - 2. * f);
  return textureLod(u_noiseTexture, fract((i + f + .5) / vec2(50.)), 0.).rg;
}

float getDrops(vec2 uv, vec2 seedShift) {
  vec2 iDropsUV = floor(uv);
  vec2 fDropsUV = fract(uv);
  float dropsMinDist = 1.;
  for (int y = -1; y < 2; y += 1) {
    for (int x = -1; x < 2; x += 1) {
      vec2 neighbor = vec2(float(y), float(x));
      vec2 offset = hash22(iDropsUV + neighbor + 50. * seedShift);
      vec2 pos = neighbor + offset - fDropsUV;
      dropsMinDist *= min(1., dot(pos, pos));
    }
  }
  return .5 * lst(.09, .08, sqrt(sqrt(dropsMinDist)));
}

vec2 getCellTilt(float idx, float radius) {
  vec2 rand = hash22(vec2(idx + 31., idx * u_seed + 17.));
  float an = rand.x * TWO_PI;
  return vec2(cos(an), sin(an)) * mix(.3, 1.7, rand.y * rand.y) * mix(.7, 1., radius);
}

vec2 getCrumpleDetail(vec2 uv, float freq, float shift, float basePixel, out float depth) {
  depth = 0.;
  vec2 v = uv * freq;
  float pixel = .9 * freq * basePixel;
  vec2 base = floor(v);
  float n1 = 9., n2 = 9.;
  vec2 s1 = vec2(0.), s2 = vec2(0.), q1 = vec2(0.), q2 = vec2(0.);

  for (int y = -1; y <= 1; y++) {
    for (int x = -1; x <= 1; x++) {
      vec2 cell = base + vec2(float(x), float(y));
      vec2 q = hash22(vec2(cell.y * 1.9 + 3.1 + shift, cell.x * 1.3 + .11 * u_seed));
      float keep = .8;
      if (q.y > keep) continue;

      vec2 r = hash22(vec2(cell.x + .37 * u_seed + shift + 11.1, cell.y - .21 * u_seed + 5.7));
      vec2 s = cell + .06 + .88 * r;

      vec2 d = v - s;
      float dsq = dot(d, d);
      if (dsq < n1) {
        n2 = n1;
        s2 = s1;
        q2 = q1;
        n1 = dsq;
        s1 = s;
        q1 = q;
      } else if (dsq < n2) {
        n2 = dsq;
        s2 = s;
        q2 = q;
      }
    }
  }

  if (n1 > 8.) return vec2(0.);

  float an1 = q1.x * TWO_PI, mag1 = q1.y / .8;
  vec2 t1 = vec2(cos(an1), sin(an1)) * mix(.3, 1.7, mag1 * mag1);
  float an2 = q2.x * TWO_PI, mag2 = q2.y / .8;
  vec2 t2 = vec2(cos(an2), sin(an2)) * mix(.3, 1.7, mag2 * mag2);

  vec2 span = s1 - s2;
  float spanLen = max(length(span), 1e-4);
  float toEdge = (n2 - n1) / (2. * spanLen);

  float pair = hash21(s1 + s2 + 1.7 * abs(s1 - s2));
  float pairSoft = .05 + .1 * step(pair, .4);
  float b = clamp(toEdge / max(1.5 * pixel, .5 * pairSoft), 0., 1.);
  b = b * b * (3. - 2. * b);
  float d1 = max(sqrt(n1), 1e-4);
  depth = .2 * d1;
  float shoulderAmt = max(0., 1. - toEdge / .42) * b;
  vec2 shoulder = vec2(0.);
  if (shoulderAmt > 0.) {
    float d2 = max(sqrt(n2), 1e-4);
    shoulder = shoulderAmt * ((v - s2) / d2 - (v - s1) / d1);
  }

  vec2 mid = .5 * (t1 + t2);
  return mid + (t1 - mid) * b + .9 * shoulder;
}

vec4 getCrumples(vec2 uv) {
  float crumpleN = max(2., floor(u_crumpleCount + .5));
  float near = 60000., nearB = 60000.;
  float idx = 0., rad = 0.;
  vec2 nearP = vec2(0.), nearPb = vec2(0.);
  vec4 seeds[${J.maxCrumpleCount}];
  for (int i = 0; i < ${J.maxCrumpleCount}; i++) {
    if (float(i) >= crumpleN) break;
    vec2 rand = hash22(vec2(float(i), float(i) * u_seed));
    float an = rand.x * TWO_PI;
    vec2 p = vec2(cos(an), sin(an)) * rand.y;
    seeds[i] = vec4(p, rand);

    vec2 d = uv - p;
    float dsq = dot(d, d);
    if (dsq < near) {
      nearB = near;
      nearPb = nearP;
      near = dsq;
      idx = float(i);
      rad = rand.y;
      nearP = p;
    } else if (dsq < nearB) {
      nearB = dsq;
      nearPb = p;
    }
  }
  float l = sqrt(near), lb = sqrt(nearB);
  vec2 dir = (uv - nearP) / max(l, 1e-4);
  float edge = lst(0., .5, lb - l);

  float shoulderLimit = l + .5;

  vec2 tilt = getCellTilt(idx, rad);
  float tiltSum = 1.;
  vec2 wide = vec2(0.);
  float wideSum = 0.;
  float toEdge = 9., edgeSoft = 0.;

  for (int i = 0; i < ${J.maxCrumpleCount}; i++) {
    if (float(i) >= crumpleN) break;
    if (float(i) == idx) continue;
    vec4 seed = seeds[i];
    vec2 p = seed.xy;
    vec2 rand = seed.zw;

    vec2 d = uv - p;
    float dsq = dot(d, d);

    if (dsq < shoulderLimit * shoulderLimit) {
      float di = sqrt(dsq);
      float shoulder = lst(.5, 0., di - l);
      wide += shoulder * (d / max(di, 1e-4) - dir);
      wideSum += shoulder;
    }

    float pairSoft = .01 + .05 * step(.2, rand.x) + .3 * rand.y;
    float toBisector = (dsq - near) / (.2 * max(length(p - nearP), 1e-4));
    if (toBisector < toEdge) {
      toEdge = toBisector;
      edgeSoft = pairSoft;
    }

    float w = 1. - toBisector / pairSoft;
    if (w > 0.) {
      w *= w;
      tilt += w * getCellTilt(float(i), rand.y);
      tiltSum += w;
    }
  }

  float blend = clamp(toEdge / edgeSoft, 0., 1.);
  vec2 sharp = (1. - edge) * ((uv - nearPb) / max(lb, 1e-4) - dir);
  vec2 rounding = mix(sharp, wide / max(wideSum, 1.), min(2. * edgeSoft, 1.));
  float radial = l / max(l + toEdge, 1e-4);

  return vec4(tilt / tiltSum + rounding * blend * radial, .2 * l, edge);
}

void getFolds(vec2 coord, vec2 offset, vec2 count, vec2 noise, vec2 baseFwidth, out vec2 slope, out vec2 dark, out vec2 lift) {
  vec2 g = coord * count + .5 * offset + noise;
  vec2 dx = fract(g) - .5;
  vec2 adx = abs(dx);
  float foldRadius = .3;
  vec2 t = clamp(adx / foldRadius, 0., 1.);
  dark = t * t * (3. - 2. * t);
  lift = 1. - t;
  lift *= lift;

  vec2 crease = clamp(dx / max(count * baseFwidth, 1e-5), -1., 1.);
  slope = crease * (1. - dark);

  vec2 lineWidth = .02 * count * mix(vec2(2.5), vec2(.8), dark.yx) + .1 * noise;
  slope -= 1. - smoothstep(vec2(0.), lineWidth, adx);
}

void main() {

  vec2 patternUV = (v_imageUV - .5) * vec2(u_imageAspectRatio, 1.);
  float basePixel = max(length(dFdx(patternUV)), length(dFdy(patternUV)));
  vec2 baseFwidth = fwidth(patternUV);

  float pattern = 0.;

  float crumpleDepth = 0.;
  float wrinkleDepth = 0.;
  float foldDepth = 0.;
  float roughness = 0.;
  float fiber = 0.;
  float drops = 0.;

  float grazing = 0.7;
  float lightRad = radians(u_angle);
  vec2 lightDir = vec2(sin(lightRad), -cos(lightRad));
  float foldAngleInSector = mod(u_angle, 90.);
  float foldLightRad = radians(u_angle - foldAngleInSector + mix(12., 78., foldAngleInSector / 90.));
  vec2 foldLightDir = vec2(sin(foldLightRad), -cos(foldLightRad));
  vec2 relief = vec2(0.);
  vec2 foldSlope = vec2(0.);
  float reliefAmount = 0.;
  float foldInk = 1.;
  vec2 crumpleFlow = vec2(0.);

  vec2 warpNoise = vec2(0.);
  if (u_crumples > 0. || u_folds > 0. || u_wrinkles > 0.) {
    warpNoise = smoothNoise(patternUV * .1 + .2 + .6 * fract(.017 * u_seed)) - .5;
  }
  vec2 crumplesUV = (patternUV * .9) + .012 * warpNoise;

  if (u_crumples > 0.) {
    vec4 crumples = getCrumples(crumplesUV);

    vec2 crumpleTilt = .5 * u_crumples * crumples.xy;
    crumpleTilt -= .1 * max(dot(crumpleTilt, lightDir), 0.) * lightDir;
    relief += crumpleTilt;
    reliefAmount += .6 * u_crumples;
    crumpleFlow = crumples.w * crumpleTilt;

    crumpleDepth = clamp(5. * crumples.z, 0., 1.);
  }

  if (u_wrinkles > 0.) {
    float detailFreq = mix(10., 1., u_wrinkleSize);
    float detailAmp = .2;
    vec2 detailGrad = vec2(0.);
    float detailDepth = 0., depthSum = 0.;
    for (int i = 0; i < 3; i++) {
      float layerDepth;
      detailGrad += detailAmp * getCrumpleDetail(crumplesUV, detailFreq, 31. * float(i), basePixel, layerDepth);
      detailDepth += detailAmp * layerDepth;
      depthSum += detailAmp;
      detailAmp *= (.5 + .2 * detailGrad.x);
      detailFreq *= 2.1;
    }

    vec2 detailTilt = 1.5 * u_wrinkles * detailGrad;
    detailTilt -= .65 * max(dot(detailTilt, lightDir), 0.) * lightDir;
    relief += detailTilt;
    reliefAmount += .6 * u_wrinkles;

    wrinkleDepth = clamp(5. * detailDepth / max(depthSum, 1e-4), 0., 1.);
  }

  if (u_folds > 0.) {
    vec2 foldOffset = vec2(1. - 2. * u_foldOffsetX, 1. - 2. * u_foldOffsetY);
    vec2 foldCount = vec2(mix(3., .5, u_foldSizeX), mix(3., .5, u_foldSizeY));
    vec2 foldNoise = .005 * warpNoise * foldCount;
    vec2 uv = patternUV + .03 * crumpleFlow;

    vec2 slope, dark, lift;
    getFolds(uv, foldOffset, foldCount, foldNoise, baseFwidth, slope, dark, lift);

    foldSlope = u_folds * slope;
    reliefAmount += 1. * u_folds;
    vec2 ink = mix(vec2(.96), vec2(1.), dark);
    float flatness = dark.x * dark.y;
    foldInk *= mix(1., ink.x * ink.y * mix(.5, .3, flatness), u_folds);

    foldDepth = u_folds * (lift.y * foldLightDir.y - lift.x * foldLightDir.x);
  }

  float unlit = cos(grazing);
  float lit = unlit;

  if (reliefAmount > 0.) {
    float lightFalloff = clamp(.5 + dot(v_imageUV - .5, lightDir), 0., 1.);
    float lightPower = mix(.5, 1., lightFalloff);
    float slope = clamp(dot(relief, lightDir) + dot(foldSlope, foldLightDir), -1.2, 1.2);
    lit = max(cos(slope + grazing), 0.);

    pattern += (clamp(reliefAmount, 0., 1.) * unlit + (lit - unlit)) * foldInk * lightPower;
  }

  patternUV += .04 * crumpleFlow;

  vec2 seedShift = floor(fract(u_seed * vec2(.7548776662, .5698402909)) * 50.) / 50.;

  if (u_roughness > 0.) {
    roughness = u_roughness * getRoughness(1000. * patternUV, lightDir, seedShift, basePixel);
    pattern += roughness;
  }

  if (u_fiber > 0.) {
    fiber = u_fiber * getFiber(50. * patternUV, seedShift, basePixel);
    pattern += fiber;
  }

  if (u_drops > 0.) {
    drops = u_drops * getDrops(patternUV * 10., seedShift);
  }

  pattern = clamp(pattern, 0., 1.);

  vec3 backColor = u_colorBack.rgb * u_colorBack.a;
  float backOpacity = u_colorBack.a;
  vec3 baseColor = u_colorPaper.rgb * u_colorPaper.a;
  float baseOpacity = u_colorPaper.a;
  vec3 shadowColor = u_colorShadow.rgb * u_colorShadow.a;
  float shadowOpacity = u_colorShadow.a;

  float notClipped = u_clip ? .1 : 1.;
  vec2 imageCenteredUV = v_imageUV - .5;
  float edgeDist = 2. * max(abs(imageCenteredUV.x), abs(imageCenteredUV.y));
  float wrinkleDistortion = mix(.38, .0175, smoothstep(0., 1., edgeDist));
  float scaleDistortion = .15 * u_crumples * (crumpleDepth - .5) + wrinkleDistortion * u_wrinkles * (wrinkleDepth - .5) + .05 * foldDepth;
  vec2 linearDistortion = notClipped * .002 * lightDir * drops;
  float radialDistortion = notClipped * .02 * (roughness + fiber);
  vec2 centeredUV = imageCenteredUV * (1. - u_distortion * scaleDistortion);
  centeredUV -= u_distortion * linearDistortion;
  vec2 imageUV = .5 + centeredUV * (1. - abs(u_distortion) * radialDistortion * dot(centeredUV, centeredUV));

  vec3 color = shadowColor * pattern;
  float opacity = shadowOpacity * pattern;
  color += baseColor * (1. - opacity);
  opacity += baseOpacity * (1. - opacity);

  if (u_isImage) {
    float frame = getUvFrame(imageUV);
    vec4 image = texture(u_image, imageUV);
    frame *= image.a;

    float maxC = max(max(image.r, image.g), image.b);
    float minC = min(min(image.r, image.g), image.b);
    float sat = maxC > 0. ? (maxC - minC) / maxC : 0.;
    float midC = image.r + image.g + image.b - maxC - minC;
    float secondaryness = maxC > minC ? (midC - minC) / (maxC - minC) : 0.;
    float satDampen = sat * (1. - .5 * secondaryness);
    float darkDampen = 1. - dot(vec3(.2126, .7152, .0722), image.rgb);
    float dampen = mix(0., .7, u_blending) * max(satDampen, darkDampen);

    vec3 paper = vec3(1.) - opacity + color;
    vec3 pic = image.rgb * paper * u_blending + image.rgb * (1. - u_blending);
    pic = mix(pic, vec3(1.), .6 * pow(dampen, 2. + 3. * pattern));

    color = mix(color, pic, frame);
    opacity = frame + opacity * (1. - frame);

    if (u_clip) {
      color *= frame;
      opacity *= frame;
    }
  }

  color *= mix(vec3(1.), .5 * u_colorShadow.rgb, drops * shadowOpacity);

  color += backColor * (1. - opacity);
  opacity += backOpacity * (1. - opacity);

  fragColor = vec4(color, opacity);
}
`;var Re={maxSamples:50},ne=`#version 300 es
precision mediump float;

uniform sampler2D u_image;
uniform float u_imageAspectRatio;
uniform float u_spread;
uniform float u_bias;
uniform float u_angle;
uniform float u_perspective;
uniform float u_count;
uniform float u_dispersion;
uniform float u_dispersionShift;
uniform float u_dispersionColor;
uniform float u_focusCenter;
uniform float u_focusEdges;
uniform float u_swirl;
uniform float u_noise;
uniform float u_noiseFrequency;
uniform float u_noiseOffset;
uniform float u_lensBulge;
uniform float u_lensCircle;
uniform float u_grainMixer;
uniform float u_grainOverlay;
uniform float u_imageX;
uniform float u_imageY;

in vec2 v_imageUV;

out vec4 fragColor;

${M}
${O}
${Q}

float valueNoise(vec2 st) {
  vec2 i = floor(st);
  vec2 f = fract(st);
  float a = hash21(i);
  float b = hash21(i + vec2(1., 0.));
  float c = hash21(i + vec2(0., 1.));
  float d = hash21(i + vec2(1., 1.));
  vec2 u = f * f * (3. - 2. * f);
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float getUvFrame(vec2 uv) {
  vec2 invAA = 1. / clamp(fwidth(uv), 1e-5, .02);
  vec2 lo = clamp(uv * invAA + .5, 0., 1.);
  vec2 hi = clamp((1. - uv) * invAA + .5, 0., 1.);
  return lo.x * hi.x * lo.y * hi.y;
}

vec4 sampleOverWhite(vec2 uv) {
  vec4 img = texture(u_image, uv);
  float cover = img.a * getUvFrame(uv);
  vec3 colorOverWhite = mix(vec3(1.), img.rgb, cover);
  return vec4(colorOverWhite, cover);
}

vec3 hueColor(float hue) {
  vec3 rgb = clamp(abs(mod(hue * 6. + vec3(0., 4., 2.), 6.) - 3.) - 1., 0., 1.);
  rgb = rgb * rgb * (3. - 2. * rgb);
  return rgb;
}
                                            
float boxInradius() {
  return .5 * min(u_imageAspectRatio, 1.);
}

float boxOutradius() {
  return .5 * length(vec2(u_imageAspectRatio, 1.));
}

float spreadReach() {
  return .7 * pow(u_spread, 1.3 + 2.7 * u_spread);
}

float innerCircleMask(float radius, float inradius) {
  return 1. - smoothstep(.5 * inradius, 1.1 * inradius, radius);
}

float dispersionCurve(float t, float biasPow) {
  float mirroredT = u_bias < 0. ? 1. - t : t;
  float curved = pow(mirroredT, biasPow);
  return u_bias < 0. ? 1. - curved : curved;
}

vec2 getSpread(vec2 fromCenter, float radius, vec2 warpedUV, float edgeAA, float inradius, float outradius, float reach, out float outStrength) {
  float angleRad = radians(u_angle);
  vec2 uniformDir = vec2(cos(angleRad), sin(angleRad));

  float invInradius = 1. / inradius;
  vec2 radialDir = fromCenter * invInradius;
  float maxLen = (outradius + reach) * invInradius;
  float radialLen = radius * invInradius;
  if (radialLen > maxLen) radialDir *= maxLen / radialLen;
  vec2 spreadDir = mix(uniformDir, radialDir, u_perspective);

  float bandProximity = smoothstep(inradius * .8, inradius, radius);
  float lensCircleMaxing = bandProximity * u_lensCircle * u_lensCircle * u_lensCircle;
  spreadDir = mix(spreadDir, radialDir, lensCircleMaxing);

  vec2 warpedAbs = abs(warpedUV - .5);
  float inner = mix(1., smoothstep(0., mix(inradius, outradius, u_focusCenter), radius), u_focusCenter);
  float boxDist = max(warpedAbs.x, warpedAbs.y) * 2.;
  float outer = mix(1., 1. - min(boxDist, 1.), u_focusEdges);
  float strength = inner * outer;

  strength *= mix(1., mix(.15, .03, max(-u_lensBulge, 0.)), lensCircleMaxing);

  vec2 outside = max(warpedAbs - .5, 0.);
  float margin = max(reach * length(spreadDir) * (1. - u_lensCircle), edgeAA);
  strength *= 1. - smoothstep(0., margin, length(outside));

  outStrength = strength;
  vec2 axis = spreadDir * (reach * strength);

  if (u_noise > 0.) {
    float turn = (valueNoise(fromCenter * u_noiseFrequency * 18. + u_noiseOffset * 30.) - .5) * 2. * u_noise;
    float cs = cos(turn), sn = sin(turn);
    axis = mat2(cs, -sn, sn, cs) * axis;
  }

  axis.x /= u_imageAspectRatio;
  return axis;
}

vec2 lensWarp(vec2 fromCenter, float radius, float inradius, out float bulgeFade) {
  bulgeFade = 1.;
  if (u_lensBulge == 0. && u_lensCircle <= 0.) return fromCenter;
  if (radius < 1e-5) return fromCenter;
  
  float r = radius;

  if (u_lensBulge != 0.) {
    float rn = radius / inradius;
    float bulge = abs(u_lensBulge) * (u_lensBulge > 0. ? 1.4 : 1.2);
    if (u_lensBulge > 0.) bulgeFade = 1. - smoothstep(1.45, 1.53, rn * bulge);
    float map = u_lensBulge > 0.
      ? tan(min(rn * bulge, 1.53)) / tan(bulge)
      : atan(rn * tan(bulge)) / bulge;
    float bulgeScale = map / rn;
    fromCenter *= bulgeScale;
    r *= bulgeScale;
  }

  if (u_lensCircle > 0.) {
    vec2 dir = fromCenter / max(r, 1e-5);
    vec2 halfBox = vec2(u_imageAspectRatio, 1.) * .5;
    float rBox = min(halfBox.x / max(abs(dir.x), 1e-4), halfBox.y / max(abs(dir.y), 1e-4));
    float band = inradius * mix(.03, .2, .5 * (u_lensBulge + 1.));
    float innerEdge = inradius - band;
    float over = smoothstep(0., 1., (r - innerEdge) / band);
    float g = r + (rBox - inradius) * pow(over, 14.);
    fromCenter = dir * mix(r, g, u_lensCircle);
  }

  return fromCenter;
}

void main() {
  vec2 uv = v_imageUV;

  float invAspect = 1. / u_imageAspectRatio;
  float inradius = boxInradius();
  float outradius = boxOutradius();
  float reach = spreadReach();

  vec2 fromCenter = uv - .5;
  fromCenter.x *= u_imageAspectRatio;
  float radius = length(fromCenter);

  float bulgeFade;
  vec2 warpedFromCenter = lensWarp(fromCenter, radius, inradius, bulgeFade);
  vec2 baseUV = vec2(warpedFromCenter.x * invAspect, warpedFromCenter.y) + .5;

  vec2 baseDerivative = fwidth(baseUV);
  float edgeAA = clamp(2. * max(baseDerivative.x, baseDerivative.y), .001, .02);

  float spreadStrength;
  vec2 spreadAxis = getSpread(fromCenter, radius, baseUV, edgeAA, inradius, outradius, reach, spreadStrength);

  vec2 grainUV = vec2(0.);
  if (u_grainMixer > 0. || u_grainOverlay > 0.) {
    vec2 dudx = dFdx(v_imageUV);
    vec2 dudy = dFdy(v_imageUV);
    grainUV = (v_imageUV - .5) * (.8 / vec2(length(dudx), length(dudy))) + .5;
  }

  if (u_grainMixer > 0.) {
    float grainSeed = valueNoise(grainUV);
    vec2 jit = fract(grainSeed * vec2(157.31, 113.57)) * 2. - 1.;
    spreadAxis += jit * .3 * u_grainMixer * length(spreadAxis);
  }

  float count = floor(u_count);
  int countInteger = int(count);

  float invCount = 1. / count;
  float invSpan = 1. / max(count - 1., 1.);
  float hueBase = u_dispersionColor + .5;
  float biasPower = 1. + 2. * abs(u_bias);
  vec2 tapBias = .5 - vec2(u_imageX, u_imageY);

  float centerAmount = clamp(1. - u_dispersionShift, 0., 1.);
  float edgesAmount = clamp(1. + u_dispersionShift, 0., 1.);
  float dispersion = u_dispersion * mix(edgesAmount, centerAmount, innerCircleMask(radius, inradius));
  float dispersionPower = pow(dispersion, .8);

  float warpedRadius = length(warpedFromCenter);
  float swirlAngle = u_swirl * .4 * PI * spreadStrength * u_spread * min(1., inradius / max(warpedRadius, 1e-4));

  vec3 colorSum = vec3(0.);
  vec3 weightSum = vec3(0.);
  float coverSum = 0.;

  for (int i = 0; i < ${Re.maxSamples}; i++) {
    if (i >= countInteger) break;

    float layer = float(i);
    float hue = hueBase + layer * invCount;

    float spread = layer * invSpan;
    if (u_bias != 0.) spread = dispersionCurve(spread, biasPower);

    // mix(axis, -axis, spread) is axis scaled by the signed position along the fan
    float fanPos = 1. - 2. * spread;
    vec2 tapUV = baseUV + spreadAxis * fanPos - .5;

    if (u_swirl != 0.) {
      tapUV.x *= u_imageAspectRatio;
      tapUV = rotate(tapUV, swirlAngle * fanPos);
      tapUV.x *= invAspect;
    }

    vec4 tap = sampleOverWhite(tapUV + tapBias);
    vec3 weight = 1. - dispersionPower * hueColor(hue);

    colorSum += tap.rgb * weight;
    weightSum += weight;
    coverSum += tap.a;
  }

  vec3 color = colorSum / max(weightSum, 1e-4);
  float coverAvg = coverSum * invCount;

  float ground = min(color.r, min(color.g, color.b));
  float alpha = max(coverAvg, 1. - ground);
  vec3 premult = max(color - (1. - alpha), 0.);
  fragColor = vec4(premult, alpha) * bulgeFade;

  if (u_grainOverlay > 0.) {
    float grain = valueNoise(rotate(grainUV, 1.) + vec2(3.));
    grain = mix(grain, valueNoise(rotate(grainUV, 2.) + vec2(-1.)), .5);
    grain = pow(grain, 1.3);
    float grainV = grain * 2. - 1.;
    float grainStrength = pow(u_grainOverlay * abs(grainV), .8) * fragColor.a;
    fragColor.rgb = mix(fragColor.rgb, vec3(step(0., grainV)) * fragColor.a, .35 * grainStrength);
  }
}
`;var ce={maxColorCount:10},ue=`#version 300 es
precision highp float;

in mediump vec2 v_imageUV;
in mediump vec2 v_objectUV;
out vec4 fragColor;

uniform sampler2D u_image;
uniform float u_time;

uniform vec4 u_colorBack;
uniform vec4 u_colors[${ce.maxColorCount}];
uniform float u_colorsCount;

uniform float u_angle;
uniform float u_noise;
uniform float u_innerGlow;
uniform float u_outerGlow;
uniform float u_contour;

#define TWO_PI 6.28318530718
#define PI 3.14159265358979323846

float getImgFrame(vec2 uv, float th) {
  float frame = 1.;
  frame *= smoothstep(0., th, uv.y);
  frame *= 1. - smoothstep(1. - th, 1., uv.y);
  frame *= smoothstep(0., th, uv.x);
  frame *= 1. - smoothstep(1. - th, 1., uv.x);
  return frame;
}

float circle(vec2 uv, vec2 c, vec2 r) {
  return 1. - smoothstep(r[0], r[1], length(uv - c));
}

float lst(float edge0, float edge1, float x) {
  return clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
}

float sst(float edge0, float edge1, float x) {
  return smoothstep(edge0, edge1, x);
}

float shadowShape(vec2 uv, float t, float contour) {
  vec2 scaledUV = uv;

  // base shape tranjectory
  float posY = mix(-1., 2., t);

  // scaleX when it's moving down
  scaledUV.y -= .5;
  float mainCircleScale = sst(0., .8, posY) * lst(1.4, .9, posY);
  scaledUV *= vec2(1., 1. + 1.5 * mainCircleScale);
  scaledUV.y += .5;

  // base shape
  float innerR = .4;
  float outerR = 1. - .3 * (sst(.1, .2, t) * (1. - sst(.2, .5, t)));
  float s = circle(scaledUV, vec2(.5, posY - .2), vec2(innerR, outerR));
  float shapeSizing = sst(.2, .3, t) * sst(.6, .3, t);
  s = pow(s, 1.4);
  s *= 1.2;

  // flat gradient to take over the shadow shape
  float topFlattener = 0.;
  {
    float pos = posY - uv.y;
    float edge = 1.2;
    topFlattener = lst(-.4, 0., pos) * (1. - sst(.0, edge, pos));
    topFlattener = pow(topFlattener, 3.);
    float topFlattenerMixer = (1. - sst(.0, .3, pos));
    s = mix(topFlattener, s, topFlattenerMixer);
  }

  // apple right circle
  {
    float visibility = sst(.6, .7, t) * (1. - sst(.8, .9, t));
    float angle = -2. -t * TWO_PI;
    float rightCircle = circle(uv, vec2(.95 - .2 * cos(angle), .4 - .1 * sin(angle)), vec2(.15, .3));
    rightCircle *= visibility;
    s = mix(s, 0., rightCircle);
  }

  // apple top circle
  {
    float topCircle = circle(uv, vec2(.5, .19), vec2(.05, .25));
    topCircle += 2. * contour * circle(uv, vec2(.5, .19), vec2(.2, .5));
    float visibility = .55 * sst(.2, .3, t) * (1. - sst(.3, .45, t));
    topCircle *= visibility;
    s = mix(s, 0., topCircle);
  }

  float leafMask = circle(uv, vec2(.53, .13), vec2(.08, .19));
  leafMask = mix(leafMask, 0., 1. - sst(.4, .54, uv.x));
  leafMask = mix(0., leafMask, sst(.0, .2, uv.y));
  leafMask *= (sst(.5, 1.1, posY) * sst(1.5, 1.3, posY));
  s += leafMask;

  // apple bottom circle
  {
    float visibility = sst(.0, .4, t) * (1. - sst(.6, .8, t));
    s = mix(s, 0., visibility * circle(uv, vec2(.52, .92), vec2(.09, .25)));
  }

  // random balls that are invisible if apple logo is selected
  {
    float pos = sst(.0, .6, t) * (1. - sst(.6, 1., t));
    s = mix(s, .5, circle(uv, vec2(.0, 1.2 - .5 * pos), vec2(.1, .3)));
    s = mix(s, .0, circle(uv, vec2(1., .5 + .5 * pos), vec2(.1, .3)));

    s = mix(s, 1., circle(uv, vec2(.95, .2 + .2 * sst(.3, .4, t) * sst(.7, .5, t)), vec2(.07, .22)));
    s = mix(s, 1., circle(uv, vec2(.95, .2 + .2 * sst(.3, .4, t) * (1. - sst(.5, .7, t))), vec2(.07, .22)));
    s /= max(1e-4, sst(1., .85, uv.y));
  }

  s = clamp(0., 1., s);
  return s;
}

float blurEdge3x3(sampler2D tex, vec2 uv, vec2 dudx, vec2 dudy, float radius, float centerSample) {
  vec2 texel = 1.0 / vec2(textureSize(tex, 0));
  vec2 r = radius * texel;

  float w1 = 1.0, w2 = 2.0, w4 = 4.0;
  float norm = 16.0;
  float sum = w4 * centerSample;

  sum += w2 * textureGrad(tex, uv + vec2(0.0, -r.y), dudx, dudy).g;
  sum += w2 * textureGrad(tex, uv + vec2(0.0, r.y), dudx, dudy).g;
  sum += w2 * textureGrad(tex, uv + vec2(-r.x, 0.0), dudx, dudy).g;
  sum += w2 * textureGrad(tex, uv + vec2(r.x, 0.0), dudx, dudy).g;

  sum += w1 * textureGrad(tex, uv + vec2(-r.x, -r.y), dudx, dudy).g;
  sum += w1 * textureGrad(tex, uv + vec2(r.x, -r.y), dudx, dudy).g;
  sum += w1 * textureGrad(tex, uv + vec2(-r.x, r.y), dudx, dudy).g;
  sum += w1 * textureGrad(tex, uv + vec2(r.x, r.y), dudx, dudy).g;

  return sum / norm;
}

void main() {
  vec2 uv = v_objectUV + .5;
  uv.y = 1. - uv.y;

  vec2 imgUV = v_imageUV;
  imgUV -= .5;
  imgUV *= 0.5714285714285714;
  imgUV += .5;
  float imgSoftFrame = getImgFrame(imgUV, .03);

  vec4 img = texture(u_image, imgUV);
  vec2 dudx = dFdx(imgUV);
  vec2 dudy = dFdy(imgUV);

  if (img.a == 0.) {
    fragColor = u_colorBack;
    return;
  }

  float t = .1 * u_time;
  t -= .3;

  float tCopy = t + 1. / 3.;
  float tCopy2 = t + 2. / 3.;

  t = mod(t, 1.);
  tCopy = mod(tCopy, 1.);
  tCopy2 = mod(tCopy2, 1.);

  vec2 animationUV = imgUV - vec2(.5);
  float angle = -u_angle * PI / 180.;
  float cosA = cos(angle);
  float sinA = sin(angle);
  animationUV = vec2(
  animationUV.x * cosA - animationUV.y * sinA,
  animationUV.x * sinA + animationUV.y * cosA
  ) + vec2(.5);

  float shape = img[0];

  img[1] = blurEdge3x3(u_image, imgUV, dudx, dudy, 8., img[1]);

  float outerBlur = 1. - mix(1., img[1], shape);
  float innerBlur = mix(img[1], 0., shape);
  float contour = mix(img[2], 0., shape);

  outerBlur *= imgSoftFrame;

  float shadow = shadowShape(animationUV, t, innerBlur);
  float shadowCopy = shadowShape(animationUV, tCopy, innerBlur);
  float shadowCopy2 = shadowShape(animationUV, tCopy2, innerBlur);

  float inner = .8 + .8 * innerBlur;
  inner = mix(inner, 0., shadow);
  inner = mix(inner, 0., shadowCopy);
  inner = mix(inner, 0., shadowCopy2);

  inner *= mix(0., 2., u_innerGlow);

  inner += (u_contour * 2.) * contour;
  inner = min(1., inner);
  inner *= (1. - shape);

  float outer = 0.;
  {
    t *= 3.;
    t = mod(t - .1, 1.);

    outer = .9 * pow(outerBlur, .8);
    float y = mod(animationUV.y - t, 1.);
    float animatedMask = sst(.3, .65, y) * (1. - sst(.65, 1., y));
    animatedMask = .5 + animatedMask;
    outer *= animatedMask;
    outer *= mix(0., 5., pow(u_outerGlow, 2.));
    outer *= imgSoftFrame;
  }

  inner = pow(inner, 1.2);
  float heat = clamp(inner + outer, 0., 1.);

  heat += (.005 + .35 * u_noise) * (fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453123) - .5);

  float mixer = heat * u_colorsCount;
  vec4 gradient = u_colors[0];
  gradient.rgb *= gradient.a;
  float outerShape = 0.;
  for (int i = 1; i < ${ce.maxColorCount+1}; i++) {
    if (i > int(u_colorsCount)) break;
    float m = clamp(mixer - float(i - 1), 0., 1.);
    if (i == 1) {
      outerShape = m;
    }
    vec4 c = u_colors[i - 1];
    c.rgb *= c.a;
    gradient = mix(gradient, c, m);
  }

  vec3 color = gradient.rgb * outerShape;
  float opacity = gradient.a * outerShape;

  vec3 bgColor = u_colorBack.rgb * u_colorBack.a;
  color = color + bgColor * (1.0 - opacity);
  opacity = opacity + u_colorBack.a * (1.0 - opacity);

  color += .02 * (fract(sin(dot(uv + 1., vec2(12.9898, 78.233))) * 43758.5453123) - .5);

  fragColor = vec4(color, opacity);
}
`;function fe(t){let e=document.createElement("canvas"),a=1e3;return new Promise((o,i)=>{let s=new Image;s.crossOrigin="anonymous",s.addEventListener("load",()=>{(typeof t=="string"?t.endsWith(".svg"):t.type==="image/svg+xml")&&(s.width=a,s.height=a);let n=s.naturalWidth/s.naturalHeight,r=Math.floor(a*.15),m=Math.ceil(r*2.5),l=a,p=a;n>1?p=Math.floor(a/n):l=Math.floor(a*n),e.width=l+2*m,e.height=p+2*m;let f=e.getContext("2d",{willReadFrequently:!0});if(!f)throw new Error("Failed to get canvas 2d context");f.fillStyle="white",f.fillRect(0,0,e.width,e.height),f.drawImage(s,m,m,l,p);let{width:d,height:b}=e,v=f.getImageData(0,0,d,b).data,c=d*b,u=new Uint8ClampedArray(c);for(let E=0;E<c;E++){let R=E*4,U=v[R]??0,W=v[R+1]??0,L=v[R+2]??0;u[E]=.299*U+.587*W+.114*L|0}let B=r,S=Math.max(1,Math.round(.12*r)),y=5,F=le(u,d,b,B,3),I=le(u,d,b,S,3),A=le(u,d,b,y,1),C=f.createImageData(d,b),T=C.data;for(let E=0;E<c;E++){let R=E*4;T[R]=A[E]??0,T[R+1]=F[E]??0,T[R+2]=I[E]??0,T[R+3]=255}f.putImageData(C,0,0),e.toBlob(E=>{if(!E){i(new Error("Failed to create PNG blob"));return}o({blob:E})},"image/png")}),s.addEventListener("error",()=>{i(new Error("Failed to load image"))}),s.src=typeof t=="string"?t:URL.createObjectURL(t)})}function Ve(t,e,a,o){if(o<=0)return t.slice();let i=new Uint8ClampedArray(e*a),s=new Uint32Array(e*a);for(let n=0;n<a;n++){let r=0;for(let m=0;m<e;m++){let l=n*e+m,p=t[l]??0;r+=p,s[l]=r+(n>0?s[l-e]??0:0)}}for(let n=0;n<a;n++){let r=Math.max(0,n-o),m=Math.min(a-1,n+o);for(let l=0;l<e;l++){let p=Math.max(0,l-o),f=Math.min(e-1,l+o),d=m*e+f,b=m*e+(p-1),D=(r-1)*e+f,v=(r-1)*e+(p-1),c=s[d]??0,u=p>0?s[b]??0:0,B=r>0?s[D]??0:0,S=p>0&&r>0?s[v]??0:0,y=c-u-B+S,F=(f-p+1)*(m-r+1);i[n*e+l]=Math.round(y/F)}}return i}function le(t,e,a,o,i){if(o<=0||i<=1)return Ve(t,e,a,o);let s=t,n=t;for(let r=0;r<i;r++)n=Ve(s,e,a,o),s=n;return n}var me=`#version 300 es
precision mediump float;

uniform vec4 u_colorFront;
uniform vec4 u_colorBack;
uniform float u_radius;
uniform float u_contrast;

uniform sampler2D u_image;
uniform float u_imageAspectRatio;

uniform float u_size;
uniform float u_grainMixer;
uniform float u_grainOverlay;
uniform float u_grainSize;
uniform float u_grid;
uniform bool u_originalColors;
uniform bool u_inverted;
uniform float u_type;

in vec2 v_imageUV;

out vec4 fragColor;

${M}
${Q}
${O}

float valueNoise(vec2 st) {
  vec2 i = floor(st);
  vec2 f = fract(st);
  float a = hash21(i);
  float b = hash21(i + vec2(1.0, 0.0));
  float c = hash21(i + vec2(0.0, 1.0));
  float d = hash21(i + vec2(1.0, 1.0));
  vec2 u = f * f * (3.0 - 2.0 * f);
  float x1 = mix(a, b, u.x);
  float x2 = mix(c, d, u.x);
  return mix(x1, x2, u.y);
}

float lst(float edge0, float edge1, float x) {
  return clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
}

float sst(float edge0, float edge1, float x) {
  return smoothstep(edge0, edge1, x);
}

float getCircle(vec2 uv, float r, float baseR) {
  r = mix(.25 * baseR, 0., r);
  float d = length(uv - .5);
  float aa = fwidth(d);
  return 1. - smoothstep(r - aa, r + aa, d);
}

float getCell(vec2 uv) {
  float insideX = step(0.0, uv.x) * (1.0 - step(1.0, uv.x));
  float insideY = step(0.0, uv.y) * (1.0 - step(1.0, uv.y));
  return insideX * insideY;
}

float getCircleWithHole(vec2 uv, float r, float baseR) {
  float cell = getCell(uv);

  r = mix(.75 * baseR, 0., r);
  float rMod = mod(r, .5);

  float d = length(uv - .5);
  float aa = fwidth(d);
  float circle = 1. - smoothstep(rMod - aa, rMod + aa, d);
  if (r < .5) {
    return circle;
  } else {
    return cell - circle;
  }
}

float getGooeyBall(vec2 uv, float r, float baseR) {
  float d = length(uv - .5);
  float sizeRadius = .3;
  if (u_grid == 1.) {
    sizeRadius = .42;
  }
  sizeRadius = mix(sizeRadius * baseR, 0., r);
  d = 1. - sst(0., sizeRadius, d);

  d = pow(d, 2. + baseR);
  return d;
}

float getSoftBall(vec2 uv, float r, float baseR) {
  float d = length(uv - .5);
  float sizeRadius = clamp(baseR, 0., 1.);
  sizeRadius = mix(.5 * sizeRadius, 0., r);
  d = 1. - lst(0., sizeRadius, d);
  float powRadius = 1. - lst(0., 2., baseR);
  d = pow(d, 4. + 3. * powRadius);
  return d;
}

float getUvFrame(vec2 uv, vec2 pad) {
  float aa = 0.0001;

  float left   = smoothstep(-pad.x, -pad.x + aa, uv.x);
  float right  = smoothstep(1.0 + pad.x, 1.0 + pad.x - aa, uv.x);
  float bottom = smoothstep(-pad.y, -pad.y + aa, uv.y);
  float top    = smoothstep(1.0 + pad.y, 1.0 + pad.y - aa, uv.y);

  return left * right * bottom * top;
}

float sigmoid(float x, float k) {
  return 1.0 / (1.0 + exp(-k * (x - 0.5)));
}

float getLumAtPx(vec2 uv, float contrast) {
  vec4 tex = texture(u_image, uv);
  vec3 color = vec3(
  sigmoid(tex.r, contrast),
  sigmoid(tex.g, contrast),
  sigmoid(tex.b, contrast)
  );
  float lum = dot(vec3(0.2126, 0.7152, 0.0722), color);
  lum = mix(1., lum, tex.a);
  lum = u_inverted ? (1. - lum) : lum;
  return lum;
}

float getLumBall(vec2 p, vec2 pad, vec2 inCellOffset, float contrast, float baseR, float stepSize, out vec4 ballColor) {
  p += inCellOffset;
  vec2 uv_i = floor(p);
  vec2 uv_f = fract(p);
  vec2 samplingUV = (uv_i + .5 - inCellOffset) * pad + vec2(.5);
  float outOfFrame = getUvFrame(samplingUV, pad * stepSize);

  float lum = getLumAtPx(samplingUV, contrast);
  ballColor = texture(u_image, samplingUV);
  ballColor.rgb *= ballColor.a;
  ballColor *= outOfFrame;

  float ball = 0.;
  if (u_type == 0.) {
    // classic
    ball = getCircle(uv_f, lum, baseR);
  } else if (u_type == 1.) {
    // gooey
    ball = getGooeyBall(uv_f, lum, baseR);
  } else if (u_type == 2.) {
    // holes
    ball = getCircleWithHole(uv_f, lum, baseR);
  } else if (u_type == 3.) {
    // soft
    ball = getSoftBall(uv_f, lum, baseR);
  }

  return ball * outOfFrame;
}


void main() {

  float stepMultiplier = 1.;
  if (u_type == 0.) {
    // classic
    stepMultiplier = 2.;
  } else if (u_type == 1. || u_type == 3.) {
    // gooey & soft
    stepMultiplier = 6.;
  }

  float cellsPerSide = mix(300., 7., pow(u_size, .7));
  cellsPerSide /= stepMultiplier;
  float cellSizeY = 1. / cellsPerSide;
  vec2 pad = cellSizeY * vec2(1. / u_imageAspectRatio, 1.);
  if (u_type == 1. && u_grid == 1.) {
    // gooey diagonal grid works differently
    pad *= .7;
  }

  vec2 uv = v_imageUV;
  uv -= vec2(.5);
  uv /= pad;

  float contrast = mix(0., 15., pow(u_contrast, 1.5));
  float baseRadius = u_radius;
  if (u_originalColors == true) {
    contrast = mix(.1, 4., pow(u_contrast, 2.));
    baseRadius = 2. * pow(.5 * u_radius, .3);
  }

  float totalShape = 0.;
  vec3 totalColor = vec3(0.);
  float totalOpacity = 0.;

  vec4 ballColor;
  float shape;
  float stepSize = 1. / stepMultiplier;
  for (float x = -0.5; x < 0.5; x += stepSize) {
    for (float y = -0.5; y < 0.5; y += stepSize) {
      vec2 offset = vec2(x, y);

      if (u_grid == 1.) {
        float rowIndex = floor((y + .5) / stepSize);
        float colIndex = floor((x + .5) / stepSize);
        if (stepSize == 1.) {
          rowIndex = floor(uv.y + y + 1.);
          if (u_type == 1.) {
            colIndex = floor(uv.x + x + 1.);
          }
        }
        if (u_type == 1.) {
          if (mod(rowIndex + colIndex, 2.) == 1.) {
            continue;
          }
        } else {
          if (mod(rowIndex, 2.) == 1.) {
            offset.x += .5 * stepSize;
          }
        }
      }

      shape = getLumBall(uv, pad, offset, contrast, baseRadius, stepSize, ballColor);
      totalColor   += ballColor.rgb * shape;
      totalShape   += shape;
      totalOpacity += shape;
    }
  }

  const float eps = 1e-4;

  totalColor /= max(totalShape, eps);
  totalOpacity /= max(totalShape, eps);

  float finalShape = 0.;
  if (u_type == 0.) {
    finalShape = min(1., totalShape);
  } else if (u_type == 1.) {
    float aa = fwidth(totalShape);
    float th = .5;
    finalShape = smoothstep(th - aa, th + aa, totalShape);
  } else if (u_type == 2.) {
    finalShape = min(1., totalShape);
  } else if (u_type == 3.) {
    finalShape = totalShape;
  }

  vec2 grainSize = mix(2000., 200., u_grainSize) * vec2(1., 1. / u_imageAspectRatio);
  vec2 grainUV = v_imageUV - .5;
  grainUV *= grainSize;
  grainUV += .5;
  if (u_grainMixer > 0.) {
    float grain = valueNoise(grainUV);
    grain = smoothstep(.55, .7 + .2 * u_grainMixer, grain);
    grain *= u_grainMixer;
    finalShape = mix(finalShape, 0., grain);
  }

  vec3 color = vec3(0.);
  float opacity = 0.;

  if (u_originalColors == true) {
    color = totalColor * finalShape;
    opacity = totalOpacity * finalShape;

    vec3 bgColor = u_colorBack.rgb * u_colorBack.a;
    color = color + bgColor * (1. - opacity);
    opacity = opacity + u_colorBack.a * (1. - opacity);
  } else {
    vec3 fgColor = u_colorFront.rgb * u_colorFront.a;
    float fgOpacity = u_colorFront.a;
    vec3 bgColor = u_colorBack.rgb * u_colorBack.a;
    float bgOpacity = u_colorBack.a;

    color = fgColor * finalShape;
    opacity = fgOpacity * finalShape;
    color += bgColor * (1. - opacity);
    opacity += bgOpacity * (1. - opacity);
  }

  if (u_grainOverlay > 0.) {
    float grainOverlay = valueNoise(rotate(grainUV, 1.) + vec2(3.));
    grainOverlay = mix(grainOverlay, valueNoise(rotate(grainUV, 2.) + vec2(-1.)), .5);
    grainOverlay = pow(grainOverlay, 1.3);

    float grainOverlayV = grainOverlay * 2. - 1.;
    vec3 grainOverlayColor = vec3(step(0., grainOverlayV));
    float grainOverlayStrength = u_grainOverlay * abs(grainOverlayV);
    grainOverlayStrength = pow(grainOverlayStrength, .8);
    color = mix(color, grainOverlayColor, .5 * grainOverlayStrength);

    opacity += .5 * grainOverlayStrength;
  }
  opacity = clamp(opacity, 0., 1.);

  fragColor = vec4(color, opacity);
}
`,de={classic:0,gooey:1,holes:2,soft:3},pe={square:0,hex:1};var ge={maxColorCount:6},he=`#version 300 es
precision mediump float;

in mediump vec2 v_imageUV;
in mediump vec2 v_objectUV;
in mediump vec2 v_responsiveUV;
in mediump vec2 v_responsiveBoxGivenSize;
                                               
out vec4 fragColor;

uniform sampler2D u_image;
uniform float u_shape;
uniform bool u_isImage;

uniform float u_time;
uniform vec4 u_colors[${ge.maxColorCount}];
uniform float u_colorsCount;
uniform vec4 u_colorBack;
uniform vec4 u_colorInner;
uniform float u_innerDistortion;
uniform float u_outerDistortion;
uniform float u_outerGlow;
uniform float u_innerGlow;
uniform float u_offset;
uniform float u_angle;
uniform float u_size;

${M}
${Q}

// 9x9 Gaussian blur on R and G channels
vec2 gaussBlur9x9RG(sampler2D tex, vec2 uv, float radius) {
  vec2 texel = 1.0 / vec2(textureSize(tex, 0));
  vec2 r = max(radius, 0.0) * texel;
  // Pascal's row 8: sum = 256, 2D norm = 65536
  const float k[9] = float[9](1.0, 8.0, 28.0, 56.0, 70.0, 56.0, 28.0, 8.0, 1.0);
  vec2 sum = vec2(0.0);

  for (int j = -4; j <= 4; ++j) {
    float wy = k[j + 4];
    for (int i = -4; i <= 4; ++i) {
      float w = k[i + 4] * wy;
      vec2 off = vec2(float(i) * r.x, float(j) * r.y);
      sum += w * texture(tex, uv + off).rg;
    }
  }

  return sum / 65536.0;
}

float sst(float a, float b, float x) {
  return smoothstep(a, b, x);
}

void main() {
  float time = u_time;

  float roundness = 0.;
  float imgAlpha = 0.;

  if (u_isImage == true) {
    // Image sampling (UV scaled inward to account for padding)
    vec2 imageUV = v_imageUV;
    imageUV -= .5;
    imageUV *= .95;
    imageUV += .5;

    // Blurred image: x = roundness, y = alpha
    vec2 blurred = gaussBlur9x9RG(u_image, imageUV, 10.);
    roundness = 1. - blurred.x;
    vec2 texelA = 1.0 / vec2(textureSize(u_image, 0));
    const float k3[3] = float[3](1.0, 2.0, 1.0);
    for (int j = -1; j <= 1; ++j) {
      for (int i = -1; i <= 1; ++i) {
        imgAlpha += k3[i + 1] * k3[j + 1] * texture(u_image, imageUV + vec2(float(i) * texelA.x, float(j) * texelA.y)).g;
      }
    }
    imgAlpha /= 16.0;
  } else {
    vec2 uv = v_objectUV + .5;
    uv.y = 1. - uv.y;
    float edge = 0.;

    if (u_shape < 1.) {
      // full-fill on canvas
      vec2 borderUV = v_responsiveUV + .5;
      vec2 mask = min(borderUV, 1. - borderUV);
      vec2 pixel_thickness = min(250. / v_responsiveBoxGivenSize, vec2(.5));
      float maskX = smoothstep(0.0, pixel_thickness.x, mask.x);
      float maskY = smoothstep(0.0, pixel_thickness.y, mask.y);
      maskX = pow(maskX, .25);
      maskY = pow(maskY, .25);
      edge = clamp(1. - maskX * maskY, 0., 1.);
    } else if (u_shape < 2.) {
      // circle
      vec2 shapeUV = uv - .5;
      shapeUV *= .67;
      edge = pow(clamp(3. * length(shapeUV), 0., 1.), 18.);
    } else if (u_shape < 3.) {
      // daisy
      vec2 shapeUV = uv - .5;
      shapeUV *= 1.68;

      float r = length(shapeUV) * 2.;
      float a = atan(shapeUV.y, shapeUV.x) + .2;
      r *= (1. + .05 * sin(3. * a + 2. * time));
      float f = abs(cos(a * 3.));
      edge = smoothstep(f, f + .7, r);
      edge *= edge;
    } else if (u_shape < 4.) {
      // diamond
      vec2 shapeUV = uv - .5;
      shapeUV = rotate(shapeUV, .25 * PI);
      shapeUV *= 1.42;
      shapeUV += .5;
      vec2 mask = min(shapeUV, 1. - shapeUV);
      vec2 pixel_thickness = vec2(.15);
      float maskX = smoothstep(0.0, pixel_thickness.x, mask.x);
      float maskY = smoothstep(0.0, pixel_thickness.y, mask.y);
      maskX = pow(maskX, .25);
      maskY = pow(maskY, .25);
      edge = clamp(1. - maskX * maskY, 0., 1.);
    } else if (u_shape < 5.) {
      // metaballs
      vec2 shapeUV = uv - .5;
      shapeUV *= 1.3;
      edge = 0.;
      for (int i = 0; i < 5; i++) {
        float fi = float(i);
        float speed = 1.5 + 2./3. * sin(fi * 12.345);
        float angle = -fi * 1.5;
        vec2 dir1 = vec2(cos(angle), sin(angle));
        vec2 dir2 = vec2(cos(angle + 1.57), sin(angle + 1.));
        vec2 traj = .4 * (dir1 * sin(time * speed + fi * 1.23) + dir2 * cos(time * (speed * 0.7) + fi * 2.17));
        float d = length(shapeUV + traj);
        edge += pow(1.0 - clamp(d, 0.0, 1.0), 4.0);
      }
      edge = 1. - smoothstep(.65, .9, edge);
      edge = pow(edge, 4.);
    }

    imgAlpha = 1. - smoothstep(.9 - 2. * fwidth(edge), .9, edge);
    roundness = 1. - edge;
  }

// Smoke UV setup
  vec2 smokeUV = v_objectUV;
  smokeUV = rotate(smokeUV, u_angle * PI / 180.);
  smokeUV *= mix(4., 1., u_size);

  // Two swirl paths: inner (shape-masked) and outer (free), each with independent distortion
  vec2 innerUV = smokeUV;
  vec2 outerUV = smokeUV;

  // Vertical displacement \u2014 applied independently to inner and outer
  innerUV.y += u_innerDistortion * (1. - sst(0., 1., length(.4 * innerUV)));
  innerUV.y -= .4 * u_innerDistortion;
  innerUV.y += .7 * u_offset * roundness;

  outerUV.y += u_outerDistortion * (1. - sst(0., 1., length(.4 * outerUV)));
  outerUV.y -= .4 * u_outerDistortion;

  float innerSwirl = u_innerDistortion * roundness;
  float outerSwirl = u_outerDistortion;

  for (int i = 1; i < 5; i++) {
    float fi = float(i);

    float stretchIn = max(length(dFdx(innerUV)), length(dFdy(innerUV)));
    float dampenIn = 1. / (1. + stretchIn * 8.);
    float sIn = innerSwirl * dampenIn;
    innerUV.x += sIn / fi * cos(time + fi * 2.9 * innerUV.y);
    innerUV.y += sIn / fi * cos(time + fi * 1.5 * innerUV.x);

    float stretchOut = max(length(dFdx(outerUV)), length(dFdy(outerUV)));
    float dampenOut = 1. / (1. + stretchOut * 8.);
    float sOut = outerSwirl * dampenOut;
    outerUV.x += sOut / fi * cos(time + fi * 2.9 * outerUV.y);
    outerUV.y += sOut / fi * cos(time + fi * 1.5 * outerUV.x);
  }

  // Smoke shapes from swirl fields
  float innerShape = exp(-1.5 * dot(innerUV, innerUV));
  float outerShape = exp(-1.5 * dot(outerUV, outerUV));

  // Visibility masks
  float outerMask = pow(u_outerGlow, 2.) * (1. - imgAlpha);
  float innerMask = (.01 + .99 * u_innerGlow) * imgAlpha;

  innerShape *= innerMask;
  outerShape *= outerMask;

  // Color gradient
  float mixer = (innerShape + outerShape) * u_colorsCount;
  vec4 gradient = u_colors[0];
  gradient.rgb *= gradient.a;

  float smokeMask = 0.;
  for (int i = 1; i < ${ge.maxColorCount+1}; i++) {
    if (i > int(u_colorsCount)) break;

    float m = sst(0., 1., clamp(mixer - float(i - 1), 0., 1.));
    if (i == 1) smokeMask = m;

    vec4 c = u_colors[i - 1];
    c.rgb *= c.a;
    gradient = mix(gradient, c, m);
  }

  // Compositing (premultiplied alpha, front-to-back)
  vec3 color = gradient.rgb * smokeMask;
  float opacity = gradient.a * smokeMask;

  float innerOpacity = u_colorInner.a * imgAlpha;
  vec3 innerColor = u_colorInner.rgb * innerOpacity;
  color += innerColor * (1.0 - opacity);
  opacity += innerOpacity * (1.0 - opacity);

  vec3 backColor = u_colorBack.rgb * u_colorBack.a;
  color += backColor * (1.0 - opacity);
  opacity += u_colorBack.a * (1.0 - opacity);

  fragColor = vec4(color, opacity);
}
`,P={measurePerformance:!1,workingSize:512,iterations:32};function ve(t){let e=document.createElement("canvas"),a=e.getContext("2d"),o=typeof t=="string"&&t.startsWith("blob:");return new Promise((i,s)=>{if(!t||!a){s(new Error("Invalid file or canvas context"));return}let n=o&&fetch(t).then(l=>l.headers.get("Content-Type")),r=new Image;r.crossOrigin="anonymous";let m=performance.now();r.onload=async()=>{let l,p=await n;p?l=p==="image/svg+xml":typeof t=="string"?l=t.endsWith(".svg")||t.startsWith("data:image/svg+xml"):l=t.type==="image/svg+xml";let f=r.width||r.naturalWidth,d=r.height||r.naturalHeight;if(l){let h=f/d;f>d?(f=4096,d=4096/h):(d=4096,f=4096*h),r.width=f,r.height=d}let b=Math.min(f,d),v=P.workingSize/b,c=Math.round(f*v),u=Math.round(d*v);P.measurePerformance&&(console.log("[Processing Mode]"),console.log(`  Original: ${f}\xD7${d}`),console.log(`  Working: ${c}\xD7${u} (${(v*100).toFixed(1)}% scale)`),v<1&&console.log(`  Speedup: ~${Math.round(1/(v*v))}\xD7`)),e.width=f,e.height=d;let B=.025,S=Math.ceil(c*B),y=Math.ceil(u*B),F=c-2*S,I=u-2*y,A=document.createElement("canvas");A.width=c,A.height=u;let C=A.getContext("2d");C.drawImage(r,S,y,F,I);let T=performance.now(),R=C.getImageData(0,0,c,u).data,U=new Uint8Array(c*u),W=new Uint8Array(c*u),L=0;for(let g=0,h=0;g<R.length;g+=4,h++){let _=R[g+3]===0?0:1;U[h]=_,L+=_}let Z=[],N=[];for(let g=0;g<u;g++)for(let h=0;h<c;h++){let w=g*c+h;if(!U[w])continue;let _=!1;h===0||h===c-1||g===0||g===u-1?_=!0:_=!U[w-1]||!U[w+1]||!U[w-c]||!U[w+c]||!U[w-c-1]||!U[w-c+1]||!U[w+c-1]||!U[w+c+1],_?(W[w]=1,Z.push(w)):N.push(w)}P.measurePerformance&&(console.log(`[Mask Building] Time: ${(performance.now()-T).toFixed(2)}ms`),console.log(`  Shape pixels: ${L} / ${c*u} (${(L/(c*u)*100).toFixed(1)}%)`),console.log(`  Interior pixels: ${N.length}`),console.log(`  Boundary pixels: ${Z.length}`));let Pe=Ne(U,W,new Uint32Array(N),new Uint32Array(Z),c,u),Ge=performance.now(),$=Ye(Pe,U,W,c,u);P.measurePerformance&&console.log(`[Poisson Solve] Time: ${(performance.now()-Ge).toFixed(2)}ms`);let ee=0,we;for(let g=0;g<N.length;g++){let h=N[g];$[h]>ee&&(ee=$[h])}let H=document.createElement("canvas");H.width=c,H.height=u;let Be=H.getContext("2d"),k=Be.createImageData(c,u);for(let g=0;g<u;g++)for(let h=0;h<c;h++){let w=g*c+h,_=w*4;if(!U[w])k.data[_]=255,k.data[_+1]=255,k.data[_+2]=255,k.data[_+3]=0;else{let oe=255*(1-$[w]/ee);k.data[_]=oe,k.data[_+1]=oe,k.data[_+2]=oe,k.data[_+3]=255}}Be.putImageData(k,0,0),a.imageSmoothingEnabled=!0,a.imageSmoothingQuality="high",a.drawImage(H,0,0,c,u,0,0,f,d);let V=a.getImageData(0,0,f,d),be=Math.ceil(f*B),Se=Math.ceil(d*B),te=document.createElement("canvas");te.width=f,te.height=d;let Ce=te.getContext("2d");Ce.drawImage(r,be,Se,f-2*be,d-2*Se);let Te=Ce.getImageData(0,0,f,d);for(let g=0;g<V.data.length;g+=4){let h=Te.data[g+3],w=V.data[g+3];h===0?(V.data[g]=255,V.data[g+1]=0):(V.data[g]=w===0?0:V.data[g],V.data[g+1]=h),V.data[g+2]=255,V.data[g+3]=255}a.putImageData(V,0,0),we=V,e.toBlob(g=>{if(!g){s(new Error("Failed to create PNG blob"));return}if(P.measurePerformance){let h=performance.now()-m;if(console.log(`[Total Processing Time] ${h.toFixed(2)}ms`),v<1){let w=h*Math.pow(f*d/(c*u),1.5);console.log(`[Estimated time at full resolution] ~${w.toFixed(0)}ms`),console.log(`[Time saved] ~${(w-h).toFixed(0)}ms (${Math.round(w/h)}\xD7 faster)`)}}i({imageData:we,pngBlob:g})},"image/png")},r.onerror=()=>s(new Error("Failed to load image")),r.src=typeof t=="string"?t:URL.createObjectURL(t)})}function Ne(t,e,a,o,i,s){let n=a.length,r=new Int32Array(n*4);for(let m=0;m<n;m++){let l=a[m],p=l%i,f=Math.floor(l/i);r[m*4+0]=p<i-1&&t[l+1]?l+1:-1,r[m*4+1]=p>0&&t[l-1]?l-1:-1,r[m*4+2]=f>0&&t[l-i]?l-i:-1,r[m*4+3]=f<s-1&&t[l+i]?l+i:-1}return{interiorPixels:a,boundaryPixels:o,pixelCount:n,neighborIndices:r}}function Ye(t,e,a,o,i){let s=P.iterations,n=.01,r=new Float32Array(o*i),{interiorPixels:m,neighborIndices:l,pixelCount:p}=t,f=performance.now(),d=1.9,b=[],D=[];for(let c=0;c<p;c++){let u=m[c],B=u%o,S=Math.floor(u/o);(B+S)%2===0?b.push(c):D.push(c)}for(let c=0;c<s;c++){for(let u of b){let B=m[u],S=l[u*4+0],y=l[u*4+1],F=l[u*4+2],I=l[u*4+3],A=0;S>=0&&(A+=r[S]),y>=0&&(A+=r[y]),F>=0&&(A+=r[F]),I>=0&&(A+=r[I]);let C=(n+A)/4;r[B]=d*C+(1-d)*r[B]}for(let u of D){let B=m[u],S=l[u*4+0],y=l[u*4+1],F=l[u*4+2],I=l[u*4+3],A=0;S>=0&&(A+=r[S]),y>=0&&(A+=r[y]),F>=0&&(A+=r[F]),I>=0&&(A+=r[I]);let C=(n+A)/4;r[B]=d*C+(1-d)*r[B]}}let v=new Float32Array(o*i);for(let c=0;c<3;c++){v.set(r);for(let u=0;u<p;u++){let B=m[u],S=l[u*4+0],y=l[u*4+1],F=l[u*4+2],I=l[u*4+3],A=0,C=0;S>=0&&(A+=v[S],C++),y>=0&&(A+=v[y],C++),F>=0&&(A+=v[F],C++),I>=0&&(A+=v[I],C++),r[B]=C>0?(v[B]+A/C)*.5:v[B]}}if(P.measurePerformance){let c=performance.now()-f;console.log(`[Optimized Poisson Solver (SOR \u03C9=${d})]`),console.log(`  Working size: ${o}\xD7${i}`),console.log(`  Iterations: ${s}`),console.log(`  Time: ${c.toFixed(2)}ms`),console.log(`  Interior pixels processed: ${p}`),console.log(`  Speed: ${(s*p/(c*1e3)).toFixed(2)} Mpixels/sec`)}return r}var xe={none:0,circle:1,daisy:2,diamond:3,metaballs:4};function X(t){if(Array.isArray(t))return t.length===4?t:t.length===3?[...t,1]:z;if(typeof t!="string")return z;let e,a,o,i=1;if(t.startsWith("#"))[e,a,o,i]=He(t);else if(t.startsWith("rgb")){let s=qe(t);if(s===null)return z;[e,a,o,i]=s}else if(t.startsWith("hsl")){let s=Je(t);if(s===null)return z;[e,a,o,i]=Ke(s)}else return console.error("Unsupported color format",t),z;return[K(e,0,1),K(a,0,1),K(o,0,1),K(i,0,1)]}function He(t){if(t=t.replace(/^#/,""),(t.length===3||t.length===4)&&(t=t.split("").map(s=>s+s).join("")),t.length===6&&(t=t+"ff"),!/^[0-9a-f]{8}$/i.test(t))return console.warn("Invalid hex color"),z;let e=parseInt(t.slice(0,2),16)/255,a=parseInt(t.slice(2,4),16)/255,o=parseInt(t.slice(4,6),16)/255,i=parseInt(t.slice(6,8),16)/255;return[e,a,o,i]}function qe(t){let e=t.match(/^rgba?\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*(?:,\s*([0-9.]+))?\s*\)$/i);return e?[parseInt(e[1]??"0")/255,parseInt(e[2]??"0")/255,parseInt(e[3]??"0")/255,e[4]===void 0?1:parseFloat(e[4])]:null}function Je(t){let e=t.match(/^hsla?\s*\(\s*(\d+)\s*,\s*(\d+)%\s*,\s*(\d+)%\s*(?:,\s*([0-9.]+))?\s*\)$/i);return e?[parseInt(e[1]??"0"),parseInt(e[2]??"0"),parseInt(e[3]??"0"),e[4]===void 0?1:parseFloat(e[4])]:null}function Ke(t){let[e,a,o,i]=t,s=e/360,n=a/100,r=o/100,m,l,p;if(a===0)m=l=p=r;else{let f=(D,v,c)=>(c<0&&(c+=1),c>1&&(c-=1),c<.16666666666666666?D+(v-D)*6*c:c<.5?v:c<.6666666666666666?D+(v-D)*(.6666666666666666-c)*6:D),d=r<.5?r*(1+n):r+n-r*n,b=2*r-d;m=f(b,d,s+1/3),l=f(b,d,s),p=f(b,d,s-1/3)}return[m,l,p,i]}var K=(t,e,a)=>Math.min(Math.max(t,e),a),z=[.5,.5,.5,1];function Ae(){if(typeof window>"u")return;let t=new Image;return t.src=Xe,t}var Xe="data:image/png;base64,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";var x={"graphite-600":"#4A4F5B","graphite-700":"#353943","graphite-800":"#2A2D34","graphite-850":"#22242A","graphite-900":"#1E1E1E","graphite-950":"#14151A","red-50":"#FBEAE9","red-100":"#F7D0CF","red-200":"#F1A9A7","red-300":"#EA7E7B","red-400":"#DE615E","red-500":"#CF3430","red-600":"#AC2623","red-700":"#8C1F1D","red-800":"#691917","red-900":"#461210","red-950":"#290B0A","bg-deep":"#1E1E1E","bg-pane":"#14151A","bg-chrome":"#22242A","bg-raised":"#2A2D34","bg-hover":"#353943",line:"rgba(255, 255, 255, 0.08)","line-strong":"rgba(255, 255, 255, 0.14)",ink:"#F3F4F6","ink-2":"rgba(243, 244, 246, 0.74)","ink-3":"rgba(243, 244, 246, 0.54)","ink-4":"rgba(243, 244, 246, 0.30)",accent:"#EA7E7B","accent-hover":"#F1A9A7","accent-strong":"#DE615E","accent-ink":"#FFFFFF","accent-fill":"#AC2623","accent-fill-hover":"#CF3430","accent-dim":"rgba(234, 126, 123, 0.14)","accent-line":"rgba(234, 126, 123, 0.4)",highlight:"rgba(249, 242, 188, 0.16)","highlight-line":"rgba(249, 242, 188, 0.62)",focus:"#F3F4F6",marker:"#AC2623",rec:"#FF453A",wait:"#D7B07A",play:"#F3F4F6","pad-paper":"#F9F2BC","pad-shade":"#EEE4A4","pad-rule":"#A7D0D2","pad-ink":"#1C1917","pad-ink-2":"rgba(28, 25, 23, 0.68)","pad-accent":"#AC2623"};var G={fit:"contain",scale:1,rotation:0,offsetX:0,offsetY:0,originX:.5,originY:.5,worldWidth:0,worldHeight:0},je={...G,fit:"none"},Me={"booth-glow":{shader:"grainGradient",where:"Site hero ground, cover, section bands",params:{...je,speed:.35,colorBack:x["graphite-900"],colors:[x["graphite-900"],x["red-950"],x["red-900"],x["red-800"]],softness:1,intensity:.22,noise:.32,shape:"corners"}},"pad-card":{shader:"paperTexture",where:"Tier cards, lyric sheet, share card",params:{...G,fit:"cover",speed:0,colorBack:x["pad-shade"],colorPaper:x["pad-paper"],colorShadow:"#D6CB86",blending:1,distortion:.2,clip:!1,angle:300,seed:4,roughness:.28,roughnessSize:.35,roughnessRows:.35,fiber:.3,fiberSize:.4,folds:.18,foldSizeX:1,foldSizeY:1,foldOffsetX:0,foldOffsetY:0,wrinkles:.35,wrinkleSize:.5,crumples:0,crumpleCount:6,drops:.12}},"wait-smoke":{shader:"gemSmoke",where:"Plugin disconnected empty state",image:"../brand/two-glyph-booth.svg",minPixelRatio:1,maxPixelCount:600*600,params:{...G,scale:.72,speed:.45,colorBack:x["graphite-950"],colorInner:x["graphite-950"],colors:[x.wait,"#8C6B3C",x.ink],outerGlow:.35,innerGlow:.9,innerDistortion:.8,outerDistortion:.55,offset:0,angle:0,size:.8,shape:"none"}},"idle-smoke":{shader:"gemSmoke",where:"Plugin idle empty state",image:"../brand/two-glyph-booth.svg",minPixelRatio:1,maxPixelCount:600*600,params:{...G,scale:.72,speed:.45,colorBack:x["graphite-950"],colorInner:x["graphite-950"],colors:[x["graphite-600"],"#B9BDC7",x.ink],outerGlow:.35,innerGlow:.9,innerDistortion:.8,outerDistortion:.55,offset:0,angle:0,size:.8,shape:"none"}},"punch-heat":{shader:"heatmap",where:"Site brand band, cover, launch posts",image:"../brand/fist-booth.svg",params:{...G,scale:.8,speed:.6,contour:.6,angle:0,noise:.1,innerGlow:.75,outerGlow:.6,colorBack:x["graphite-900"],colors:[x["red-900"],x["red-700"],x["red-600"],x["red-300"],x.ink]}},"flyer-dots":{shader:"halftoneDots",where:"Share cards, creators page photos, merch",image:"../brand/fist-pad.svg",params:{...G,fit:"contain",scale:.9,speed:0,colorBack:x["pad-paper"],colorFront:x["pad-ink"],size:.32,radius:1.2,contrast:.55,originalColors:!1,inverted:!1,grainMixer:.15,grainOverlay:.15,grainSize:.5,grid:"hex",type:"gooey"}},"lost-lens":{shader:"lensDistortion",where:"404 page",image:"../brand/wordmark-booth.svg",params:{...G,scale:.9,fit:"contain",speed:0,spread:.8,bias:1,angle:0,perspective:.1,count:40,dispersion:1,dispersionShift:0,dispersionColor:.6,focusCenter:.8,focusEdges:1,swirl:.5,noise:0,noiseFrequency:.25,noiseOffset:0,lensBulge:.3,lensCircle:0,grainMixer:0,grainOverlay:0,imageX:0,imageY:0}}};var Ze=["fit","scale","rotation","offsetX","offsetY","originX","originY","worldWidth","worldHeight"],ke={fit:"contain",scale:1,rotation:0,offsetX:0,offsetY:0,originX:.5,originY:.5,worldWidth:0,worldHeight:0},$e={grainGradient:{fragment:ie,enums:{shape:re},noise:!0},paperTexture:{fragment:se,image:"optional",isImageFlag:!0,noise:!0,mipmaps:["u_image"]},gemSmoke:{fragment:he,enums:{shape:xe},image:"optional",isImageFlag:!0,process:t=>ve(t).then(e=>e.pngBlob),mipmaps:["u_image"]},heatmap:{fragment:ue,image:"required",process:t=>fe(t).then(e=>e.blob),mipmaps:["u_image"]},halftoneDots:{fragment:me,enums:{type:de,grid:pe},image:"optional"},lensDistortion:{fragment:ne,image:"optional",mipmaps:["u_image"]}};function et(){try{return!!document.createElement("canvas").getContext("webgl2")}catch{return!1}}function tt(){try{return window.matchMedia("(prefers-reduced-motion: reduce)").matches}catch{return!1}}function ot(t){return t.complete&&t.naturalWidth>0?Promise.resolve(t):new Promise((e,a)=>{t.addEventListener("load",()=>e(t),{once:!0}),t.addEventListener("error",()=>a(new Error(`p2p-shaders: could not load ${t.src.slice(0,60)}`)),{once:!0})})}function Qe(t){return new Promise((e,a)=>{let o=new Image;o.onload=()=>{if(o.naturalWidth<1024&&o.naturalHeight<1024&&o.naturalWidth>0){let i=o.naturalWidth/o.naturalHeight;o.width=Math.round(i>1?1024*i:1024),o.height=Math.round(i>1?1024:1024/i)}e(o)},o.onerror=()=>a(new Error(`p2p-shaders: could not load image ${t}`)),o.src=t})}async function at(t,e){if(!e)return;if(!t.process)return Qe(e);let a=await t.process(e);return Qe(URL.createObjectURL(a))}function it(t,e,a){let o={};for(let[i,s]of Object.entries(e))i==="speed"||i==="frame"||i==="image"||(i==="fit"?o.u_fit=q[s]:i==="colors"?(o.u_colors=s.map(X),o.u_colorsCount=s.length):/^color[A-Z]/.test(i)?o[`u_${i}`]=X(s):t.enums&&t.enums[i]?o[`u_${i}`]=t.enums[i][s]:o[`u_${i}`]=s);for(let i of Ze)!(`u_${i}`in o)&&i!=="fit"&&(o[`u_${i}`]=ke[i]);return"u_fit"in o||(o.u_fit=q[ke.fit]),t.image&&(o.u_image=a??rt()),t.isImageFlag&&(o.u_isImage=!!a),t.noise&&(o.u_noiseTexture=Ae()),o}var j;function rt(){return j||(j=new Image,j.src="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg=="),j}async function st(t,e,a={},o={}){let i=Me[e];if(!i)throw new Error(`p2p-shaders: unknown preset "${e}"`);if(!et())return t.dataset.p2pShaderState="fallback",null;let s=$e[i.shader],n={...i.params,...a},r=o.image??i.image;if(s.image==="required"&&!r)throw new Error(`p2p-shaders: "${e}" needs an image`);let m=await at(s,r),p=o.static||tt()?0:n.speed??0,f=it(s,n,m);await Promise.all(Object.values(f).filter(b=>b instanceof HTMLImageElement).map(ot));let d=new Y(t,s.fragment,f,void 0,p,n.frame??0,o.minPixelRatio??i.minPixelRatio??2,o.maxPixelCount??i.maxPixelCount,s.mipmaps);return t.dataset.p2pShaderState="mounted",d}async function Yt(t=document){let e=[...t.querySelectorAll("[data-p2p-shader]")];return Promise.all(e.map(a=>{let o={};try{o=a.dataset.p2pParams?JSON.parse(a.dataset.p2pParams):{}}catch{console.warn("p2p-shaders: bad data-p2p-params on",a)}return st(a,a.dataset.p2pShader,o,{image:a.dataset.image,static:"p2pStatic"in a.dataset}).catch(i=>(a.dataset.p2pShaderState="error",console.warn(i),null))}))}export{Me as PRESETS,Yt as autoMount,st as mountShader,et as webgl2Available};
