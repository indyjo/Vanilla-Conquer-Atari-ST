import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import tailwindcss from '@tailwindcss/vite';

export default defineConfig({
  // Relative paths so the built site works on itch.io and other subpath hosts.
  base: './',
  plugins: [tailwindcss(), svelte()],
  // Workers load remix.js via fetch (Vite module workers cannot use importScripts).
  worker: {
    format: 'es',
  },
  build: {
    target: 'es2022',
  },
});
