'use strict';

// Live 320x240 theme preview using the launcher's real 8x8 font.
(function () {
  const W = 320, H = 240, FW = 8, FH = 8;
  const MENU = ['PLAY', 'LOAD from SD', 'TOOLS', 'FUN', 'SETTINGS'];

  class ThemePreview {
    constructor(canvas) {
      this.canvas = canvas;
      this.ctx = canvas.getContext('2d');
      this.ctx.imageSmoothingEnabled = false;
      this.theme = null;
      this.screen = 'home';
      this.font = null;
      this.mask = null;
      this.cache = new Map();
      this.loadFont();
    }

    async loadFont() {
      const img = new Image();
      img.src = window.PICO_FONT_URL || '/assets/font_bold_8x8.bmp';
      try {
        await img.decode();
      } catch {
        this.fontError = true;
        return;
      }
      // Convert the black-on-white bitmap into an opaque-white alpha mask.
      const c = document.createElement('canvas');
      c.width = 2048;
      c.height = 8;
      const g = c.getContext('2d');
      g.drawImage(img, 0, 0);
      const data = g.getImageData(0, 0, c.width, c.height);
      const d = data.data;
      for (let i = 0; i < d.length; i += 4) {
        const lum = (d[i] + d[i + 1] + d[i + 2]) / 3;
        if (lum < 128) {
          d[i] = 255; d[i + 1] = 255; d[i + 2] = 255; d[i + 3] = 255;
        } else {
          d[i + 3] = 0;
        }
      }
      g.putImageData(data, 0, 0);
      this.mask = c;
      this.font = img;
      if (this.theme) this.draw();
    }

    glyph(code, color, scale) {
      const key = `${code}|${color}|${scale}`;
      let g = this.cache.get(key);
      if (g) return g;

      const c = document.createElement('canvas');
      c.width = FW * scale;
      c.height = FH * scale;
      const x = c.getContext('2d');
      x.imageSmoothingEnabled = false;
      if (this.mask) {
        x.drawImage(this.mask, (code & 0xff) * FW, 0, FW, FH, 0, 0, c.width, c.height);
        x.globalCompositeOperation = 'source-in';
        x.fillStyle = '#' + color;
        x.fillRect(0, 0, c.width, c.height);
      } else {
        x.fillStyle = '#' + color;
        x.font = `${FH * scale}px monospace`;
        x.textBaseline = 'top';
        x.fillText(String.fromCharCode(code), 0, 0);
      }
      this.cache.set(key, c);
      if (this.cache.size > 900) this.cache.clear();
      return c;
    }

    text(str, x, y, color, scale = 1, align = 0) {
      if (!str) return;
      const s = scale >= 2 ? 2 : 1;
      const w = str.length * FW * s;
      if (x < 0 || align === 1) x = Math.floor((W - w) / 2);
      else if (align === 2) x = (x > 0 ? x : W - FW * s) - w;
      const ctx = this.ctx;
      for (let i = 0; i < str.length; i++) {
        const code = str.charCodeAt(i);
        if (code < 32) continue;
        const g = this.glyph(code, color, s);
        ctx.drawImage(g, Math.round(x + i * FW * s), Math.round(y));
      }
    }

    rect(x, y, w, h, color) {
      this.ctx.fillStyle = '#' + color;
      this.ctx.fillRect(Math.round(x), Math.round(y), Math.round(w), Math.round(h));
    }

    frame(x, y, w, h, color, th) {
      if (w <= 0 || h <= 0) return;
      this.rect(x, y, w, th, color);
      this.rect(x, y + h - th, w, th, color);
      this.rect(x, y + th, th, h - 2 * th, color);
      this.rect(x + w - th, y + th, th, h - 2 * th, color);
    }

    line(x1, y1, x2, y2, color, th) {
      const ctx = this.ctx;
      ctx.strokeStyle = '#' + color;
      ctx.lineWidth = th;
      ctx.beginPath();
      ctx.moveTo(x1 + th / 2, y1 + th / 2);
      ctx.lineTo(x2 + th / 2, y2 + th / 2);
      ctx.stroke();
    }

    circle(x, y, r, color, fill) {
      const ctx = this.ctx;
      ctx.beginPath();
      ctx.arc(x, y, Math.max(0, r), 0, Math.PI * 2);
      if (fill) {
        ctx.fillStyle = '#' + color;
        ctx.fill();
      } else {
        ctx.strokeStyle = '#' + color;
        ctx.lineWidth = 1;
        ctx.stroke();
      }
    }

    clip() {
      this.ctx.save();
      this.ctx.beginPath();
      this.ctx.rect(0, 0, W, H);
      this.ctx.clip();
    }

    setTheme(t) { this.theme = t; this.draw(); }
    setScreen(s) { this.screen = s; this.draw(); }

    draw() {
      if (!this.theme) return;
      const ctx = this.ctx;
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.clearRect(0, 0, W, H);
      this.clip();
      try {
        if (this.screen === 'home') this.drawHome();
        else if (this.screen === 'boot') this.drawList(this.theme.boot, 'boot');
        else this.drawList(this.theme.saver, 'saver');
      } catch (e) {
        ctx.fillStyle = '#300';
        ctx.fillRect(0, 0, W, H);
        ctx.fillStyle = '#f88';
        ctx.font = '12px monospace';
        ctx.fillText('preview error: ' + e.message, 8, 20);
      }
      ctx.restore();
    }

    drawHome() {
      const t = this.theme;
      const ctx = this.ctx;

      this.rect(0, 0, W, H, t.menu_bg);

      // Titles
      this.text(t.home_title1, -1, t.menu_title_y, t.menu_title, t.menu_title_scale, 1);
      this.text(t.home_title2, -1, t.menu_title2_y, t.menu_title, t.menu_title_scale, 1);

      // Clock (top right)
      if (t.home_time) {
        const x = t.home_time_x < 0 ? W - 8 * FW - 8 : t.home_time_x;
        const y = t.home_time_y < 0 ? t.menu_title_y + 4 : t.home_time_y;
        this.text('12:34:56', x, y, t.home_time_color, 1, 0);
      }

      // Divider
      this.rect(40, t.menu_divider_y, W - 80, 1, t.menu_dim);

      // Menu entries
      for (let i = 0; i < MENU.length; i++) {
        const y = t.menu_start_y + i * t.menu_line_h;
        const sel = i === 0;
        let color = sel ? t.menu_sel : t.menu_text;
        let arrow = '';

        if (sel) {
          if (t.menu_sel_style === 0) {
            this.rect(54, y - 2, W - 108, 12, t.menu_hl);
            arrow = '>';
          } else if (t.menu_sel_style === 1) {
            arrow = '>';
          } else if (t.menu_sel_style === 2) {
            this.rect(54, y - 2, W - 108, 12, t.menu_sel);
            color = t.menu_bg;
          }
        }
        if (arrow) this.text(arrow, 30, y, t.menu_sel, 1, 0);
        this.text(MENU[i], 60, y, color, 1, 0);
      }

      // Footer
      this.rect(20, 202, W - 40, 1, t.menu_dim);
      if (t.menu_show_footer) {
        this.text('Game: Example', 20, t.menu_footer_y, t.ok, 1, 0);
      }

      // Status row
      if (t.menu_show_status) {
        const sy = 226;
        this.text('SD', 20, sy, t.ok, 1, 0);
        this.text('USB', 46, sy, t.ok, 1, 0);
        this.text('WiFi', 86, sy, t.ok, 1, 0);
        if (t.home_weather) {
          this.text('12.3C Clear', 130, sy, t.home_weather_color, 1, 0);
        }
        this.text('82%', W - 58, sy, t.ok, 1, 0);
      }
    }

    drawList(list, which) {
      const t = this.theme;
      const ctx = this.ctx;
      this.rect(0, 0, W, H, t.bg);

      const time = which === 'boot' ? 0 : 4200;
      for (const e of list) this.drawElem(e, time);

      if (which === 'boot') {
        if (t.boot_show_version) this.text('v1.2.9', 4, H - 12, t.menu_dim, 1, 0);
      }
    }

    drawElem(e, time) {
      const t = this.theme;
      switch (e.type) {
        case 'text':
          this.text(e.text, e.x, e.y, e.color, e.scale, e.align);
          break;
        case 'blink':
          this.text(e.text, e.x, e.y, e.color, e.scale, e.align);
          break;
        case 'marquee': {
          const vis = Math.max(1, Math.floor(((e.w > 0 ? e.w : W - Math.max(0, e.x)) || W) / FW));
          this.text(e.text.slice(0, vis), e.x, e.y, e.color, 1, 0);
          break;
        }
        case 'duck': {
          for (let i = 0; i < t.duck.length; i++) {
            this.text(t.duck[i], e.x, e.y + i * 12, t.duck_color, 1, 0);
          }
          break;
        }
        case 'bar':
          this.rect(e.x, e.y, e.w, e.h, '333333');
          this.rect(e.x, e.y, Math.round(e.w * 0.5), e.h, e.color);
          break;
        case 'pct':
          this.text('50%', e.x, e.y, e.color, 1, e.x < 0 ? 1 : 0);
          break;
        case 'spinner':
          this.text('|', e.x, e.y, e.color, 1, 0);
          break;
        case 'status': {
          const rows = [
            ['USB', 'connected', t.ok],
            ['SD', 'card present', t.ok],
            ['Battery', '82%', t.ok],
          ];
          rows.forEach((r, i) => {
            this.text(r[0], e.x, e.y + i * 22, t.menu_dim, 1, 0);
            this.text(r[1], e.x + 90, e.y + i * 22, r[2], 1, 0);
          });
          break;
        }
        case 'usb': this.text('USB', e.x, e.y, t.ok, 1, 0); break;
        case 'sd': this.text('SD', e.x, e.y, t.ok, 1, 0); break;
        case 'battery': this.text('82%', e.x, e.y, t.ok, 1, 0); break;
        case 'weather': this.text('12.3C Clear', e.x, e.y, e.color, 1, 0); break;
        case 'time':
          this.text('12:34:56', e.x, e.y, e.color, e.scale, e.align);
          break;
        case 'rect':
          this.rect(e.x, e.y, e.w, e.h, e.color);
          break;
        case 'frame':
          this.frame(e.x, e.y, e.w, e.h, e.color, e.scale || 1);
          break;
        case 'line':
          this.line(e.x, e.y, e.x + e.w, e.y + e.h, e.color, e.scale || 1);
          break;
        case 'circle':
          this.circle(e.x, e.y, e.w, e.color, false);
          break;
        case 'disc':
          this.circle(e.x, e.y, e.w, e.color, true);
          break;
        default:
          break;
      }
    }
  }

  window.ThemePreview = ThemePreview;
})();
