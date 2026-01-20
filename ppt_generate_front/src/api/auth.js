import axios from 'axios'

const API_URL = import.meta.env.VITE_API_URL || '/api'

export const apiClient = axios.create({
  baseURL: API_URL,
  headers: {
    'Content-Type': 'application/json'
  }
})

export const setAuthToken = (token) => {
  if (token) {
    apiClient.defaults.headers.common.Authorization = `Bearer ${token}`
  } else {
    delete apiClient.defaults.headers.common.Authorization
  }
}

const savedToken = localStorage.getItem('token')
if (savedToken) {
  setAuthToken(savedToken)
}

// 请求拦截器
apiClient.interceptors.request.use(
  config => {
    const token = localStorage.getItem('token')
    if (token) {
      config.headers.Authorization = `Bearer ${token}`
    }
    return config
  },
  error => {
    return Promise.reject(error)
  }
)

// 响应拦截器
apiClient.interceptors.response.use(
  response => response,
  error => {
    if (error.response?.status === 401) {
      localStorage.removeItem('token')
      window.location.href = '/login'
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
  }
}
