<template>
  <div class="login-container">
    <button class="back-home" @click="router.push('/home')" aria-label="返回主页">
      <el-icon><ArrowLeft /></el-icon>
    </button>
    <div class="login-card">
      <div class="logo-section">
        <h1>PPT智能生成系统</h1>
        <p class="subtitle">基于GenAI的自动化PPT生成平台</p>
      </div>
      
      <!-- 维护模式提示（登录时检测到 503 后展示） -->
      <div v-if="isMaintenance" class="maintenance-notice">
        <div class="maintenance-icon">🔧</div>
        <h2 class="maintenance-title">系统维护中</h2>
        <p class="maintenance-desc">系统当前正在维护升级，暂时无法登录。<br/>请稍后再试，给您带来不便深表歉意。</p>
        <button class="maintenance-retry-btn" @click="isMaintenance = false; formError = ''">
          重新尝试登录
        </button>
      </div>

      <el-form
        v-else
        ref="loginFormRef"
        :model="form"
        :rules="rules"
        label-position="top"
        class="login-form"
        autocomplete="on"
        @submit.prevent="handleLogin"
      >
        <h2>用户登录</h2>
        
        <el-form-item class="form-group" prop="username" label="用户名/邮箱">
          <el-input
            id="login-username"
            v-model="form.username"
            placeholder="请输入用户名或邮箱"
            size="large"
            name="username"
            autocomplete="username"
          >
            <template #prefix>
              <el-icon><User /></el-icon>
            </template>
          </el-input>
        </el-form-item>
        
        <el-form-item class="form-group" prop="password" label="密码">
          <el-input
            id="login-password"
            v-model="form.password"
            type="password"
            placeholder="请输入密码"
            size="large"
            show-password
            name="password"
            autocomplete="current-password"
          >
            <template #prefix>
              <el-icon><Lock /></el-icon>
            </template>
          </el-input>
        </el-form-item>
        
        <div class="form-options">
          <label class="remember-me">
            <input type="checkbox" v-model="rememberMe" />
            <span>记住我</span>
          </label>
          <a href="#" class="forgot-password" @click.prevent="showResetDialog = true">忘记密码？</a>
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

      <el-dialog
        v-model="showResetDialog"
        title="找回密码"
        width="420px"
        @closed="handleResetClosed"
      >
        <el-form
          ref="resetFormRef"
          :model="resetForm"
          :rules="resetRules"
          label-position="top"
          class="reset-form"
        >
          <el-form-item label="邮箱" prop="email">
            <el-input
              v-model="resetForm.email"
              placeholder="请输入注册邮箱"
              size="large"
              clearable
              :prefix-icon="Message"
            />
          </el-form-item>
          <el-form-item label="验证码" prop="code">
            <div class="reset-code-row">
              <el-input
                v-model="resetForm.code"
                placeholder="请输入6位验证码"
                size="large"
                clearable
                :prefix-icon="Key"
              />
              <el-button
                class="reset-code-btn"
                type="primary"
                plain
                :disabled="sendCooldown > 0"
                :loading="sendLoading"
                @click="sendResetCode"
              >
                {{ sendCooldown > 0 ? `${sendCooldown}s` : '发送验证码' }}
              </el-button>
            </div>
          </el-form-item>
          <el-form-item label="新密码" prop="password">
            <el-input
              v-model="resetForm.password"
              type="password"
              placeholder="请输入新密码"
              size="large"
              show-password
              clearable
              :prefix-icon="Lock"
              @input="handlePasswordInput"
            />
          </el-form-item>
          <el-form-item label="确认密码" prop="confirmPassword">
            <el-input
              v-model="resetForm.confirmPassword"
              type="password"
              placeholder="再次输入新密码"
              size="large"
              show-password
              clearable
              :prefix-icon="Lock"
            >
            </el-input>
          </el-form-item>
        </el-form>
        <template #footer>
          <el-button @click="showResetDialog = false">取消</el-button>
          <el-button type="primary" :loading="resetLoading" @click="confirmReset">重置密码</el-button>
        </template>
      </el-dialog>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, onMounted, onBeforeUnmount } from 'vue'
import { useRouter } from 'vue-router'
import { useStore } from 'vuex'
import { ElMessage, ElMessageBox } from 'element-plus'
import { UserFilled, User, Message, Key, Lock, ArrowLeft } from '@element-plus/icons-vue'
import authApi from '@/api/auth'

const router = useRouter()
const store = useStore()

const loading = ref(false)
const rememberMe = ref(false)
const form = reactive({
  username: '',
  password: ''
})

const formError = ref('')
const isMaintenance = ref(false)
const loginFormRef = ref(null)
const showResetDialog = ref(false)
const resetFormRef = ref(null)
const sendLoading = ref(false)
const resetLoading = ref(false)
const sendCooldown = ref(0)
const resetForm = reactive({
  email: '',
  code: '',
  password: '',
  confirmPassword: ''
})

const rules = {
  username: [
    { required: true, message: '请输入用户名或邮箱', trigger: 'blur' }
  ],
  password: [
    { required: true, message: '请输入密码', trigger: 'blur' }
  ]
}

const resetRules = {
  email: [
    { required: true, message: '请输入邮箱', trigger: ['blur', 'change'] },
    { type: 'email', message: '邮箱格式不正确', trigger: ['blur', 'change'] }
  ],
  code: [
    { required: true, message: '请输入验证码', trigger: ['blur', 'change'] },
    { min: 6, max: 6, message: '请输入6位验证码', trigger: ['blur', 'change'] }
  ],
  password: [
    { required: true, message: '请输入新密码', trigger: ['blur', 'change'] },
    { min: 6, message: '密码至少6位', trigger: ['blur', 'change'] }
  ],
  confirmPassword: [
    { required: true, message: '请再次输入新密码', trigger: ['blur', 'change'] }, // 添加这行
    {
      validator: (rule, value, callback) => {
        if (value !== resetForm.password) {
          callback(new Error('两次输入密码不一致'))
          return
        }
        callback()
      },
      trigger: ['blur', 'change']
    }
  ]
}

let cooldownTimer = null

onMounted(() => {
  // 检测是否因维护模式被重定向到登录页
  const urlParams = new URLSearchParams(window.location.search)
  if (urlParams.get('maintenance') === '1') {
    isMaintenance.value = true
    // 清除 URL 参数，避免刷新后仍显示维护界面（实际状态以后端为准）
    window.history.replaceState({}, '', '/login')
  } else {
    ElMessage.info('欢迎进入PPT自动生成系统，请登录继续')
  }
  const savedUsername = localStorage.getItem('rememberedUsername')
  const savedRemember = localStorage.getItem('rememberMe')
  if (savedRemember === 'true') {
    rememberMe.value = true
    if (savedUsername) {
      form.username = savedUsername
    }
  }
  localStorage.removeItem('rememberedPassword')
})

const handleLogin = async () => {
  if (!loginFormRef.value) return
  const valid = await loginFormRef.value.validate().catch(() => false)
  if (!valid) return
  
  formError.value = ''

  if (rememberMe.value) {
    localStorage.setItem('rememberedUsername', form.username)
    localStorage.setItem('rememberMe', 'true')
  } else {
    localStorage.removeItem('rememberedUsername')
    localStorage.setItem('rememberMe', 'false')
  }
  
  loading.value = true
  
  try {
    const response = await store.dispatch('login', {
      username: form.username,
      password: form.password,
      rememberMe: rememberMe.value
    })
    const user = response?.data?.user || store.getters.currentUser
    ElMessage.success('登录成功，正在跳转...')
    await new Promise(resolve => setTimeout(resolve, 600))
    if (user?.isAdmin) {
      router.push('/admin')
    } else {
      router.push('/main')
    }
  } catch (error) {
    console.error('登录失败:', error)
    const status = error.response?.status
    if (status === 503) {
      // 系统维护模式：显示全页维护提示
      isMaintenance.value = true
      formError.value = ''
    } else {
      const message = error.userMessage || error.response?.data?.message || '登录失败，请检查用户名和密码'
      formError.value = message
      if (message.includes('禁用')) {
        ElMessage.error('账号已被禁用！')
        await ElMessageBox.alert(
          '请联系管理员处理：bk2898453810@gmail.com',
          '账号已被禁用',
          {
            confirmButtonText: '我知道了',
            type: 'warning'
          }
        )
      }
    }
  } finally {
    loading.value = false
  }
}

const startCooldown = () => {
  sendCooldown.value = 60
  if (cooldownTimer) {
    clearInterval(cooldownTimer)
  }
  cooldownTimer = setInterval(() => {
    if (sendCooldown.value <= 1) {
      sendCooldown.value = 0
      clearInterval(cooldownTimer)
      cooldownTimer = null
      return
    }
    sendCooldown.value -= 1
  }, 1000)
}

const sendResetCode = async () => {
  if (sendLoading.value || sendCooldown.value > 0) {
    return
  }
  if (!resetFormRef.value) return
  const valid = await resetFormRef.value.validateField('email').then(() => true).catch(() => false)
  if (!valid) return

  sendLoading.value = true
  try {
    await authApi.requestPasswordReset(resetForm.email)
    ElMessage.success('验证码已发送，请查收邮箱')
    startCooldown()
  } catch (error) {
    // 拦截器已统一 ElMessage，此处仅记录
    console.error('发送验证码失败:', error.userMessage || error.response?.data?.message)
  } finally {
    sendLoading.value = false
  }
}

const confirmReset = async () => {
  if (!resetFormRef.value) return
  const valid = await resetFormRef.value.validate().catch(() => false)
  if (!valid) return

  resetLoading.value = true
  try {
    await authApi.confirmPasswordReset({
      email: resetForm.email,
      code: resetForm.code,
      password: resetForm.password
    })
    ElMessage.success('密码重置成功，请使用新密码登录')
    showResetDialog.value = false
  } catch (error) {
    // 拦截器已统一 ElMessage，此处仅记录
    console.error('重置密码失败:', error.userMessage || error.response?.data?.message)
  } finally {
    resetLoading.value = false
  }
}

const handleResetClosed = () => {
  if (resetFormRef.value) {
    resetFormRef.value.resetFields()
  }
}

const handlePasswordInput = () => {
  if (resetFormRef.value && resetForm.confirmPassword) {
    resetFormRef.value.validateField('confirmPassword').catch(() => {})
  }
}

onBeforeUnmount(() => {
  if (cooldownTimer) {
    clearInterval(cooldownTimer)
    cooldownTimer = null
  }
})
</script>

<style scoped>
.login-container {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  background: linear-gradient(135deg, #BAE6FD 0%, #E0F2FE 50%, #F0F9FF 100%);
  padding: var(--space-lg);
  position: relative;
}

.login-card {
  background: var(--color-bg-card);
  border-radius: var(--radius-dialog);
  box-shadow: var(--shadow-card-hover);
  width: 100%;
  max-width: 1000px;
  overflow: hidden;
  display: grid;
  grid-template-columns: 1fr 1fr;
  min-height: 500px;
  border: 1px solid rgba(0, 0, 0, 0.06);
}

.back-home {
  position: absolute;
  top: var(--space-lg);
  left: var(--space-lg);
  width: 44px;
  height: 44px;
  border-radius: var(--radius-btn);
  border: 1px solid rgba(14, 165, 233, 0.4);
  background: rgba(255, 255, 255, 0.85);
  color: var(--color-primary);
  display: grid;
  place-items: center;
  cursor: pointer;
  transition: color var(--transition-default), background var(--transition-default), border-color var(--transition-default);
  backdrop-filter: blur(6px);
}

.back-home:hover {
  background: rgba(255, 255, 255, 1);
  border-color: var(--color-primary);
  color: var(--color-primary-hover);
}

.logo-section {
  background: linear-gradient(135deg, #0EA5E9 0%, #38BDF8 100%);
  color: white;
  padding: var(--space-2xl);
  display: flex;
  flex-direction: column;
  justify-content: center;
  align-items: center;
  text-align: center;
}

.logo-section h1 {
  font-size: var(--text-title-page);
  margin-bottom: var(--space-sm);
  font-weight: 700;
  line-height: var(--line-height-tight);
}

.subtitle {
  font-size: var(--text-body);
  opacity: 0.9;
}

.login-form {
  padding: var(--space-2xl);
  display: flex;
  flex-direction: column;
  justify-content: center;
}

.login-form h2 {
  font-size: var(--text-title-section);
  font-weight: 600;
  margin-bottom: var(--space-xl);
  color: var(--color-text);
  line-height: var(--line-height-tight);
}

.form-group {
  margin-bottom: var(--space-lg);
}

.login-form :deep(.el-form-item__label) {
  font-weight: 500;
  color: var(--color-text-muted);
  margin-bottom: 8px;
}

.login-form :deep(.el-input__wrapper) {
  border: 2px solid var(--color-border);
  border-radius: var(--radius-input);
  box-shadow: none;
  padding: 0 var(--space-sm);
  transition: border-color var(--transition-default);
}

.login-form :deep(.el-input__wrapper.is-focus) {
  border-color: var(--color-primary);
}

.login-form :deep(.el-input__inner) {
  height: 44px;
  font-size: var(--text-body);
}

.login-form :deep(.el-form-item.is-error .el-input__wrapper) {
  border-color: #ef4444;
}

.login-form :deep(.el-form-item__error) {
  color: #ef4444;
  font-size: 0.875rem;
  margin-top: 4px;
}

.reset-form :deep(.el-form-item__label) {
  font-weight: 500;
  color: var(--color-text-muted);
}

.reset-form :deep(.el-input__wrapper) {
  border: 2px solid var(--color-border);
  border-radius: var(--radius-input);
  box-shadow: none;
  padding: 0 var(--space-sm);
}

.reset-form :deep(.el-input__wrapper.is-focus) {
  border-color: var(--color-primary);
}

.reset-form :deep(.el-input__inner) {
  height: 44px;
  font-size: 1rem;
}

.reset-form :deep(.el-form-item.is-error .el-input__wrapper) {
  border-color: #ef4444;
}

.reset-form :deep(.el-form-item__error) {
  color: #ef4444;
  font-size: 0.875rem;
  margin-top: 4px;
}

.reset-form :deep(.el-input__prefix-inner svg) {
  width: 18px;
  height: 18px;
}

.reset-code-row {
  display: flex;
  gap: 10px;
  align-items: center;
}

.reset-code-row :deep(.el-input) {
  flex: 1;
}

.reset-code-btn {
  min-width: 110px;
}

.required-star {
  color: #ef4444;
  font-weight: 600;
  margin-right: 6px;
  display: inline-flex;
  align-items: center;
}

.reset-form :deep(.el-input__prefix-inner) {
  gap: 6px;
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
  color: var(--color-text-muted);
}

.forgot-password {
  color: var(--color-primary);
  text-decoration: none;
  font-size: 0.9rem;
}

.forgot-password:hover {
  text-decoration: underline;
  color: var(--color-primary-hover);
}

.login-btn {
  background: linear-gradient(135deg, #0EA5E9 0%, #38BDF8 100%);
  color: white;
  padding: var(--space-md) var(--space-lg);
  border: none;
  border-radius: var(--radius-btn);
  font-size: var(--text-body);
  font-weight: 600;
  cursor: pointer;
  transition: transform var(--transition-default), box-shadow var(--transition-default), background var(--transition-default);
  margin-bottom: var(--space-lg);
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
  box-shadow: 0 10px 20px rgba(14, 165, 233, 0.35);
  background: linear-gradient(135deg, #0284C7 0%, #0EA5E9 100%);
}

.login-btn:disabled {
  opacity: 0.7;
  cursor: not-allowed;
}

.register-link {
  text-align: center;
  margin-bottom: 15px;
  color: var(--color-text-muted);
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
  color: var(--color-primary);
  text-decoration: none;
  font-weight: 500;
}

.register-link a:hover {
  text-decoration: underline;
  color: var(--color-primary-hover);
}

.demo-credentials {
  text-align: center;
  padding: 10px;
  background: var(--color-bg-page-alt);
  border-radius: 8px;
  margin-top: 10px;
}

.demo-credentials p {
  color: var(--color-primary-hover);
  font-size: 0.9rem;
  margin: 0;
}

/* ── 维护模式提示 ─────────────────────────────────── */
.maintenance-notice {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 40px 32px;
  text-align: center;
  animation: fadeIn 0.4s ease;
}
@keyframes fadeIn {
  from { opacity: 0; transform: translateY(8px); }
  to   { opacity: 1; transform: translateY(0); }
}
.maintenance-icon {
  font-size: 52px;
  margin-bottom: 16px;
  animation: spin 4s linear infinite;
}
@keyframes spin {
  0%   { transform: rotate(0deg); }
  20%  { transform: rotate(-15deg); }
  40%  { transform: rotate(15deg); }
  60%  { transform: rotate(-10deg); }
  80%  { transform: rotate(10deg); }
  100% { transform: rotate(0deg); }
}
.maintenance-title {
  font-size: 1.4rem;
  font-weight: 700;
  color: #92400E;
  margin: 0 0 12px;
}
.maintenance-desc {
  font-size: 0.95rem;
  color: #6B7280;
  line-height: 1.7;
  margin: 0 0 24px;
}
.maintenance-retry-btn {
  background: linear-gradient(135deg, #F59E0B 0%, #D97706 100%);
  color: #FFFFFF;
  border: none;
  border-radius: 10px;
  padding: 10px 24px;
  font-size: 0.95rem;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.2s;
  box-shadow: 0 4px 12px rgba(245, 158, 11, 0.3);
}
.maintenance-retry-btn:hover {
  transform: translateY(-2px);
  box-shadow: 0 8px 20px rgba(245, 158, 11, 0.4);
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
