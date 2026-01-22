import { createRouter, createWebHistory } from 'vue-router'
import Login from '@/views/Login.vue'
import Register from '@/views/Register.vue'
import Main from '@/views/Main.vue'
import store from '@/store'

const routes = [
  {
    path: '/',
    redirect: '/login'
  },
  {
    path: '/login',
    name: 'Login',
    component: Login,
    meta: { requiresAuth: false }
  },
  {
    path: '/register',
    name: 'Register',
    component: Register,
    meta: { requiresAuth: false }
  },
  {
    path: '/main/:section?',
    name: 'Main',
    component: Main,
    meta: { requiresAuth: true }
  }
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

// 导航守卫
router.beforeEach((to, from, next) => {
  const token = localStorage.getItem('token') || sessionStorage.getItem('token')
  const isAuthenticated = !!token
  const hasUser = !!store.state.user

  if (to.meta.requiresAuth && !isAuthenticated) {
    next('/login')
    return
  }

  if (isAuthenticated && !hasUser) {
    store.dispatch('fetchCurrentUser').catch((error) => {
      if (error?.response?.status === 401) {
        store.commit('logout')
        if (to.meta.requiresAuth) {
          router.replace('/login')
        }
      } else {
        console.error('获取用户信息失败:', error)
      }
    })
  }

  if ((to.path === '/login' || to.path === '/register') && hasUser) {
    next('/main')
    return
  }

  if (to.meta.requiresAuth && isAuthenticated) {
    store.dispatch('bootstrapSession').catch((error) => {
      console.error('会话初始化失败:', error)
      if (error?.response?.status === 401) {
        store.commit('logout')
        router.replace('/login')
      }
    })
  }

  next()
})

export default router
