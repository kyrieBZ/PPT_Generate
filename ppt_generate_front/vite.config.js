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
  // 允许通过 ngrok 等任意域名访问（仅开发）；未配置时设为 true 以便 ngrok 转发
  const serverAllowedHosts = allowedHosts.length ? allowedHosts : true

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
      allowedHosts: serverAllowedHosts,
      proxy: {
        // SSE 流式进度端点（P4.1）：独立代理项，禁用超时和缓冲
        '/api/ppt/progress/stream': {
          target: 'http://127.0.0.1:8080',
          changeOrigin: true,
          secure: false,
          ws: false,
          // SSE 连接最长 10 分钟（600s），须大于后端超时
          timeout: 660000,
          proxyTimeout: 660000,
          selfHandleResponse: false,
          configure(proxy) {
            proxy.on('proxyRes', (proxyRes) => {
              // 禁止代理层压缩 SSE 响应，确保事件实时推送
              delete proxyRes.headers['content-encoding']
            })
          }
        },
        '/api': {
          target: 'http://127.0.0.1:8080',
          changeOrigin: true,
          secure: false,
          // 模板上传时需要 LibreOffice 转换 + FastDFS 上传，耗时较长，延长超时至 5 分钟
          timeout: 300000,
          proxyTimeout: 300000,
          // 大文件响应（如批量下载 ZIP）用流式转发，不在 Node.js 层缓冲整个响应体
          selfHandleResponse: false,
          configure(proxy) {
            // 给所有转发到后端的请求添加 ngrok-skip-browser-warning header，
            // 确保经过 ngrok 代理的 img/fetch 请求不被 ngrok 拦截为 HTML 警告页
            proxy.on('proxyReq', (proxyReq) => {
              proxyReq.setHeader('ngrok-skip-browser-warning', 'true')
            })
          }
        },
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
