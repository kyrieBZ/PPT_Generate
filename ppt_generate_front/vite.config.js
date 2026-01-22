import { defineConfig, loadEnv } from 'vite'
import vue from '@vitejs/plugin-vue'
import { createHtmlPlugin } from 'vite-plugin-html'
import compression from 'vite-plugin-compression'

// https://vitejs.dev/config/
export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), '')
  const allowedHosts = env.VITE_ALLOWED_HOSTS
    ? env.VITE_ALLOWED_HOSTS.split(',').map((host) => host.trim()).filter(Boolean)
    : []
  
  return {
    plugins: [
      vue(),
      createHtmlPlugin({
        minify: true,
        inject: {
          data: {
            VITE_APP_TITLE: env.VITE_APP_TITLE,
            VITE_API_URL: env.VITE_API_URL,
            VITE_APP_ENV: env.VITE_APP_ENV,
            VITE_APP_VERSION: env.VITE_APP_VERSION
          }
        }
      }),
      compression({
        algorithm: 'gzip',
        ext: '.gz',
        threshold: 10240,
        deleteOriginFile: false
      })
    ],
    
    server: {
      port: 3000,
      host: true,
      open: true,
      allowedHosts: allowedHosts.length ? allowedHosts : undefined,
      proxy: {
        '/api': {
          target: 'http://localhost:8080',
          changeOrigin: true,
          secure: false
        }
      }
    },
    
    resolve: {
      alias: {
        '@': '/src',
        '@components': '/src/components',
        '@views': '/src/views',
        '@assets': '/src/assets',
        '@utils': '/src/utils',
        '@api': '/src/api',
        '@store': '/src/store'
      }
    },
    
    css: {
      devSourcemap: true,
      preprocessorOptions: {
        scss: {
          additionalData: `@import "@/styles/variables.scss";`
        }
      }
    },
    
    build: {
      target: 'es2015',
      outDir: 'dist',
      assetsDir: 'assets',
      sourcemap: mode !== 'production',
      minify: 'terser',
      terserOptions: {
        compress: {
          drop_console: mode === 'production',
          drop_debugger: mode === 'production'
        }
      },
      rollupOptions: {
        output: {
          chunkFileNames: 'assets/js/[name]-[hash].js',
          entryFileNames: 'assets/js/[name]-[hash].js',
          assetFileNames: 'assets/[ext]/[name]-[hash].[ext]',
          manualChunks: {
            vue: ['vue', 'vue-router', 'vuex'],
            vendor: ['axios', 'lodash-es', 'dayjs']
          }
        }
      },
      chunkSizeWarningLimit: 1000
    },
    
    optimizeDeps: {
      include: ['vue', 'vue-router', 'vuex', 'axios']
    }
  }
})
