import axios from 'axios'
import { ElMessage } from 'element-plus'

const API_URL = import.meta.env.VITE_API_URL || '/api'

export const apiClient = axios.create({
  baseURL: API_URL,
  headers: {
    'Content-Type': 'application/json',
    'ngrok-skip-browser-warning': 'true'
  }
})

export const setAuthToken = (token) => {
  if (token) {
    apiClient.defaults.headers.common.Authorization = `Bearer ${token}`
  } else {
    delete apiClient.defaults.headers.common.Authorization
  }
}

/** 从 axios 错误中解析用户可读文案，挂到 error.userMessage 供业务使用 */
export function getErrorMessage(error) {
  if (!error) return '请求失败，请稍后重试'
  const data = error.response?.data
  if (data && typeof data.message === 'string' && data.message.trim()) {
    return data.message.trim()
  }
  if (error.response) {
    const status = error.response.status
    const map = {
      400: '请求参数有误，请检查后重试',
      403: '没有权限执行该操作',
      404: '请求的资源不存在',
      409: '操作冲突，请稍后重试',
      422: '请求无法处理，请检查输入',
      500: '服务暂时不可用，请稍后重试'
    }
    return map[status] || `请求失败（${status}），请稍后重试`
  }
  if (error.code === 'ECONNABORTED' || error.message?.includes('timeout')) {
    return '请求超时，请检查网络后重试'
  }
  return '网络异常，请检查网络后重试'
}

const savedToken = localStorage.getItem('token') || sessionStorage.getItem('token')
if (savedToken) {
  setAuthToken(savedToken)
}

// 请求拦截器
apiClient.interceptors.request.use(
  config => {
    const token = localStorage.getItem('token') || sessionStorage.getItem('token')
    if (token) {
      config.headers.Authorization = `Bearer ${token}`
    }
    return config
  },
  error => {
    return Promise.reject(error)
  }
)

// 响应拦截器：统一挂载 userMessage，401 清 token 并跳转，503 由业务层自行处理，其它错误统一 ElMessage
apiClient.interceptors.response.use(
  response => response,
  error => {
    const userMessage = getErrorMessage(error)
    error.userMessage = userMessage

    const status = error.response?.status
    if (status === 401) {
      localStorage.removeItem('token')
      sessionStorage.removeItem('token')
      const path = window.location.pathname
      if (path !== '/login' && path !== '/register') {
        window.location.href = '/login'
      }
    } else if (status === 503) {
      // 维护模式：若用户已在受保护页面（非登录/注册页），重定向到登录页
      // 登录页检测到 503 后会展示维护公告界面
      const path = window.location.pathname
      if (path !== '/login' && path !== '/register' && path !== '/home') {
        window.location.href = '/login?maintenance=1'
      }
    } else {
      ElMessage.error(userMessage)
    }
    return Promise.reject(error)
  }
)

export default {
  // 登录
  login(credentials) {
    return apiClient.post('/auth/login', credentials)
  },
  
  // 注册
  register(userData) {
    return apiClient.post('/auth/register', userData)
  },
  
  // 登出
  logout() {
    return apiClient.post('/auth/logout')
  },
  
  // 获取用户信息
  getUserInfo() {
    return apiClient.get('/auth/user')
  },

  requestPasswordReset(email) {
    return apiClient.post('/auth/password/reset/request', { email })
  },

  confirmPasswordReset(payload) {
    return apiClient.post('/auth/password/reset/confirm', payload)
  },

  changePassword(currentPassword, newPassword) {
    return apiClient.post('/auth/password/change', {
      currentPassword,
      newPassword
    })
  },

  deleteAccount(password) {
    return apiClient.delete('/auth/account', { data: { password } })
  }
}
