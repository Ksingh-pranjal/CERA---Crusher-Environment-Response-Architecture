import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  base: '/CERA---Crusher-Environment-Response-Architecture/',
  plugins: [react()],
  server: {
    port: 5173
  }
});