import react from '@vitejs/plugin-react';
import fs from 'node:fs';
import path from 'node:path';
import { defineConfig } from 'vite';

// Puts the hashed asset list into the service worker so the app shell is cached on
// first visit and later loads never wait for the network.
const precache = () => ({
  name: 'precache',
  apply: 'build',
  writeBundle(opts, bundle) {
    const files = Object.keys(bundle).filter((f) => /\.(js|css|html|svg|webmanifest)$/.test(f) && f !== 'sw.js');
    const p = path.join(opts.dir, 'sw.js');
    const stamp = Object.keys(bundle).sort().join().length + '-' + Date.now();
    fs.writeFileSync(p, fs.readFileSync(p, 'utf8').replace('__FILES__', JSON.stringify(['./', ...files])).replace('__VERSION__', stamp));
  },
});

export default defineConfig({
  base: './',
  plugins: [react(), precache()],
  build: {
    target: 'es2020',
    cssCodeSplit: false,
    rollupOptions: { output: { manualChunks: { react: ['react', 'react-dom'] } } },
  },
});
