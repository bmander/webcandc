// webcandc: JavaScript helpers linked with --js-library.
addToLibrary({
  // Headless (node) harness: write an RGBA frame as $WEBCANDC_FRAMES/frame_NNNN.ppm.
  webcandc_dump_frame: function (rgba, w, h, n) {
    if (typeof process === 'undefined' || !process.env.WEBCANDC_FRAMES) return;
    var src = HEAPU32.subarray(rgba >> 2, (rgba >> 2) + w * h);
    var out = Buffer.alloc(w * h * 3);
    for (var i = 0; i < w * h; i++) {
      var c = src[i];
      out[i * 3] = (c >> 16) & 255;
      out[i * 3 + 1] = (c >> 8) & 255;
      out[i * 3 + 2] = c & 255;
    }
    var name = process.env.WEBCANDC_FRAMES + '/frame_' + String(n).padStart(4, '0') + '.ppm';
    require('fs').writeFileSync(name, Buffer.concat([Buffer.from('P6\n' + w + ' ' + h + '\n255\n'), out]));
  },
});
