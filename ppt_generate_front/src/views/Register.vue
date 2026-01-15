<template>
  <div class="register-container">
    <div class="register-card">
      <div class="logo-section">
        <h1>PPT智能生成系统</h1>
        <p class="subtitle">基于GenAI的自动化PPT生成平台</p>
      </div>
      
      <form @submit.prevent="handleRegister" class="register-form">
        <h2>用户注册</h2>
        
        <div class="form-group">
          <label for="username">用户名</label>
          <input
            id="username"
            v-model="form.username"
            type="text"
            required
            placeholder="请输入用户名"
            :class="{ 'error': errors.username }"
          />
          <div v-if="errors.username" class="error-message">{{ errors.username }}</div>
        </div>
        
        <div class="form-group">
          <label for="email">邮箱</label>
          <input
            id="email"
            v-model="form.email"
            type="email"
            required
            placeholder="请输入邮箱"
            :class="{ 'error': errors.email }"
          />
          <div v-if="errors.email" class="error-message">{{ errors.email }}</div>
        </div>
        
        <div class="form-group">
          <label for="password">密码</label>
          <input
            id="password"
            v-model="form.password"
            type="password"
            required
            placeholder="请输入密码"
            :class="{ 'error': errors.password }"
          />
          <div v-if="errors.password" class="error-message">{{ errors.password }}</div>
        </div>
        
        <div class="form-group">
          <label for="confirmPassword">确认密码</label>
          <input
            id="confirmPassword"
            v-model="form.confirmPassword"
            type="password"
            required
            placeholder="请再次输入密码"
            :class="{ 'error': errors.confirmPassword }"
          />
          <div v-if="errors.confirmPassword" class="error-message">{{ errors.confirmPassword }}</div>
        </div>
        
        <button type="submit" class="register-btn" :disabled="loading">
          <span v-if="loading">注册中...</span>
          <span v-else>立即注册</span>
        </button>

        <div v-if="formError" class="form-error">
          {{ formError }}
        </div>
        
        <div class="login-link">
          已有账号？ <router-link to="/login">立即登录</router-link>
        </div>
      </form>
      
      <div class="features">
        <h3>系统特色功能</h3>
        <div class="feature-grid">
          <div class="feature-item">
            <div class="feature-icon">🤖</div>
            <h4>GenAI智能生成</h4>
            <p>基于多维生成式AI模型，智能生成PPT内容</p>
          </div>
          <div class="feature-item">
            <div class="feature-icon">🎨</div>
            <h4>个性化定制</h4>
            <p>支持多通道指令，满足个性化需求</p>
          </div>
          <div class="feature-item">
            <div class="feature-icon">📊</div>
            <h4>图文表生成</h4>
            <p>支持文本、图片、表格智能生成与排版</p>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive } from 'vue'
import { useRouter } from 'vue-router'
import { useStore } from 'vuex'

const router = useRouter()
const store = useStore()

const loading = ref(false)
const formError = ref('')
const form = reactive({
  username: '',
  email: '',
  password: '',
  confirmPassword: ''
})

const errors = reactive({
  username: '',
  email: '',
  password: '',
  confirmPassword: ''
})

const validateForm = () => {
  let isValid = true
  
  // 重置错误
  Object.keys(errors).forEach(key => errors[key] = '')
  
  // 用户名验证
  if (!form.username.trim()) {
    errors.username = '用户名不能为空'
    isValid = false
  } else if (form.username.length < 3) {
    errors.username = '用户名至少3个字符'
    isValid = false
  }
  
  // 邮箱验证
  const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/
  if (!emailRegex.test(form.email)) {
    errors.email = '请输入有效的邮箱地址'
    isValid = false
  }
  
  // 密码验证
  if (form.password.length < 6) {
    errors.password = '密码至少6个字符'
    isValid = false
  }
  
  // 确认密码验证
  if (form.password !== form.confirmPassword) {
    errors.confirmPassword = '两次输入的密码不一致'
    isValid = false
  }
  
  return isValid
}

const handleRegister = async () => {
  if (!validateForm()) return

  formError.value = ''
  
  loading.value = true
  
  try {
    await store.dispatch('register', {
      username: form.username,
      email: form.email,
      password: form.password
    })
    
    // 注册成功后跳转到主页面
    router.push('/main')
  } catch (error) {
    console.error('注册失败:', error)
    formError.value = error.response?.data?.message || '注册失败，请重试'
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.register-container {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  padding: 20px;
}

.register-card {
  background: white;
  border-radius: 20px;
  box-shadow: 0 20px 60px rgba(0, 0, 0, 0.3);
  width: 100%;
  max-width: 1200px;
  overflow: hidden;
  display: grid;
  grid-template-columns: 1fr 1fr;
  min-height: 600px;
}

.logo-section {
  background: linear-gradient(135deg, #4f46e5 0%, #7c3aed 100%);
  color: white;
  padding: 40px;
  display: flex;
  flex-direction: column;
  justify-content: center;
  align-items: center;
  text-align: center;
}

.logo-section h1 {
  font-size: 2.5rem;
  margin-bottom: 10px;
  font-weight: bold;
}

.subtitle {
  font-size: 1.1rem;
  opacity: 0.9;
}

.register-form {
  padding: 40px;
  display: flex;
  flex-direction: column;
  justify-content: center;
}

.register-form h2 {
  font-size: 2rem;
  margin-bottom: 30px;
  color: #333;
}

.form-group {
  margin-bottom: 20px;
}

.form-group label {
  display: block;
  margin-bottom: 8px;
  font-weight: 500;
  color: #555;
}

.form-group input {
  width: 100%;
  padding: 12px 16px;
  border: 2px solid #e0e0e0;
  border-radius: 10px;
  font-size: 1rem;
  transition: border-color 0.3s;
}

.form-group input:focus {
  outline: none;
  border-color: #4f46e5;
}

.form-group input.error {
  border-color: #ef4444;
}

.error-message {
  color: #ef4444;
  font-size: 0.875rem;
  margin-top: 4px;
}

.register-btn {
  background: linear-gradient(135deg, #4f46e5 0%, #7c3aed 100%);
  color: white;
  padding: 14px;
  border: none;
  border-radius: 10px;
  font-size: 1.1rem;
  font-weight: 600;
  cursor: pointer;
  transition: transform 0.3s, box-shadow 0.3s;
  margin-top: 10px;
}

.register-btn:hover:not(:disabled) {
  transform: translateY(-2px);
  box-shadow: 0 10px 20px rgba(79, 70, 229, 0.3);
}

.register-btn:disabled {
  opacity: 0.7;
  cursor: not-allowed;
}

.login-link {
  text-align: center;
  margin-top: 20px;
  color: #666;
}

.login-link a {
  color: #4f46e5;
  text-decoration: none;
  font-weight: 500;
}

.login-link a:hover {
  text-decoration: underline;
}

.form-error {
  background: #fef2f2;
  color: #b91c1c;
  border-radius: 10px;
  padding: 10px 14px;
  text-align: center;
  font-size: 0.95rem;
  margin-top: 15px;
}

.features {
  grid-column: 1 / -1;
  background: #f8fafc;
  padding: 40px;
  border-top: 1px solid #e5e7eb;
}

.features h3 {
  text-align: center;
  font-size: 1.5rem;
  margin-bottom: 30px;
  color: #333;
}

.feature-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 30px;
}

.feature-item {
  text-align: center;
  padding: 20px;
  background: white;
  border-radius: 15px;
  box-shadow: 0 5px 15px rgba(0, 0, 0, 0.1);
  transition: transform 0.3s;
}

.feature-item:hover {
  transform: translateY(-5px);
}

.feature-icon {
  font-size: 3rem;
  margin-bottom: 15px;
}

.feature-item h4 {
  font-size: 1.2rem;
  margin-bottom: 10px;
  color: #333;
}

.feature-item p {
  color: #666;
  font-size: 0.9rem;
  line-height: 1.5;
}

@media (max-width: 768px) {
  .register-card {
    grid-template-columns: 1fr;
  }
  
  .logo-section {
    padding: 30px 20px;
  }
  
  .feature-grid {
    grid-template-columns: 1fr;
  }
}
</style>
