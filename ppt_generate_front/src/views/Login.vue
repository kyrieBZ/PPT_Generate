<template>
  <div class="login-container">
    <div class="login-card">
      <div class="logo-section">
        <h1>PPT智能生成系统</h1>
        <p class="subtitle">基于GenAI的自动化PPT生成平台</p>
      </div>
      
      <el-form
        ref="loginFormRef"
        :model="form"
        :rules="rules"
        label-position="top"
        class="login-form"
        @submit.prevent="handleLogin"
      >
        <h2>用户登录</h2>
        
        <el-form-item class="form-group" prop="username" label="用户名/邮箱">
          <el-input
            id="login-username"
            v-model="form.username"
            placeholder="请输入用户名或邮箱"
            size="large"
          />
        </el-form-item>
        
        <el-form-item class="form-group" prop="password" label="密码">
          <el-input
            id="login-password"
            v-model="form.password"
            type="password"
            placeholder="请输入密码"
            size="large"
            show-password
          />
        </el-form-item>
        
        <div class="form-options">
          <label class="remember-me">
            <input type="checkbox" v-model="rememberMe" />
            <span>记住我</span>
          </label>
          <a href="#" class="forgot-password">忘记密码？</a>
        </div>
        
        <button type="submit" class="login-btn" :disabled="loading">
          <span class="btn-content">
            <UserFilled class="btn-icon" />
            <span>{{ loading ? '登录中...' : '登录系统' }}</span>
          </span>
        </button>

        <div v-if="formError" class="form-error">
          {{ formError }}
        </div>
        
        <div class="register-link">
          还没有账号？ <router-link to="/register">立即注册</router-link>
        </div>
        
      </el-form>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useStore } from 'vuex'
import { UserFilled } from '@element-plus/icons-vue'

const router = useRouter()
const store = useStore()

const loading = ref(false)
const rememberMe = ref(false)
const form = reactive({
  username: '',
  password: ''
})

const formError = ref('')
const loginFormRef = ref(null)

const rules = {
  username: [
    { required: true, message: '请输入用户名或邮箱', trigger: 'blur' }
  ],
  password: [
    { required: true, message: '请输入密码', trigger: 'blur' }
  ]
}

onMounted(() => {
  const savedUsername = localStorage.getItem('rememberedUsername')
  if (savedUsername) {
    form.username = savedUsername
    rememberMe.value = true
  }
})

const handleLogin = async () => {
  if (!loginFormRef.value) return
  const valid = await loginFormRef.value.validate().catch(() => false)
  if (!valid) return
  
  formError.value = ''

  if (rememberMe.value) {
    localStorage.setItem('rememberedUsername', form.username)
  } else {
    localStorage.removeItem('rememberedUsername')
  }
  
  loading.value = true
  
  try {
    await store.dispatch('login', {
      username: form.username,
      password: form.password
    })
    
    router.push('/main')
  } catch (error) {
    console.error('登录失败:', error)
    formError.value = error.response?.data?.message || '登录失败，请检查用户名和密码'
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.login-container {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  padding: 20px;
}

.login-card {
  background: white;
  border-radius: 20px;
  box-shadow: 0 20px 60px rgba(0, 0, 0, 0.3);
  width: 100%;
  max-width: 1000px;
  overflow: hidden;
  display: grid;
  grid-template-columns: 1fr 1fr;
  min-height: 500px;
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

.login-form {
  padding: 40px;
  display: flex;
  flex-direction: column;
  justify-content: center;
}

.login-form h2 {
  font-size: 2rem;
  margin-bottom: 30px;
  color: #333;
}

.form-group {
  margin-bottom: 20px;
}

.login-form :deep(.el-form-item__label) {
  font-weight: 500;
  color: #555;
  margin-bottom: 8px;
}

.login-form :deep(.el-input__wrapper) {
  border: 2px solid #e0e0e0;
  border-radius: 10px;
  box-shadow: none;
  padding: 0 12px;
}

.login-form :deep(.el-input__wrapper.is-focus) {
  border-color: #4f46e5;
}

.login-form :deep(.el-input__inner) {
  height: 44px;
  font-size: 1rem;
}

.login-form :deep(.el-form-item.is-error .el-input__wrapper) {
  border-color: #ef4444;
}

.login-form :deep(.el-form-item__error) {
  color: #ef4444;
  font-size: 0.875rem;
  margin-top: 4px;
}

.form-options {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 20px;
}

.remember-me {
  display: flex;
  align-items: center;
  gap: 8px;
  cursor: pointer;
  color: #555;
}

.forgot-password {
  color: #4f46e5;
  text-decoration: none;
  font-size: 0.9rem;
}

.forgot-password:hover {
  text-decoration: underline;
}

.login-btn {
  background: linear-gradient(135deg, #4f46e5 0%, #7c3aed 100%);
  color: white;
  padding: 14px;
  border: none;
  border-radius: 10px;
  font-size: 1.1rem;
  font-weight: 600;
  cursor: pointer;
  transition: transform 0.3s, box-shadow 0.3s;
  margin-bottom: 20px;
}

.btn-content {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  width: 100%;
}

.btn-icon {
  width: 18px;
  height: 18px;
}

.login-btn:hover:not(:disabled) {
  transform: translateY(-2px);
  box-shadow: 0 10px 20px rgba(79, 70, 229, 0.3);
}

.login-btn:disabled {
  opacity: 0.7;
  cursor: not-allowed;
}

.register-link {
  text-align: center;
  margin-bottom: 15px;
  color: #666;
}

.form-error {
  background: #fef2f2;
  color: #b91c1c;
  border-radius: 10px;
  padding: 10px 14px;
  text-align: center;
  font-size: 0.95rem;
}

.register-link a {
  color: #4f46e5;
  text-decoration: none;
  font-weight: 500;
}

.register-link a:hover {
  text-decoration: underline;
}

.demo-credentials {
  text-align: center;
  padding: 10px;
  background: #f0f9ff;
  border-radius: 8px;
  margin-top: 10px;
}

.demo-credentials p {
  color: #0369a1;
  font-size: 0.9rem;
  margin: 0;
}

@media (max-width: 768px) {
  .login-card {
    grid-template-columns: 1fr;
  }
  
  .logo-section {
    padding: 30px 20px;
  }
  
  .login-form {
    padding: 30px 20px;
  }
}
</style>
