<template>
  <div class="profile-page">
    <header class="profile-header">
      <button class="back-btn" @click="router.push('/main')" aria-label="返回工作台">
        <el-icon><ArrowLeft /></el-icon>
        <span>返回工作台</span>
      </button>
      <h1 class="profile-title">个人信息</h1>
      <p class="profile-desc">查看账户信息与修改密码</p>
    </header>

    <div class="profile-content">
      <section class="profile-card profile-info-card">
        <h2 class="card-heading">
          <el-icon class="card-icon"><User /></el-icon>
          账户信息
        </h2>
        <div v-if="loading" class="card-loading">加载中...</div>
        <div v-else-if="user" class="info-grid">
          <div class="info-row">
            <span class="info-label">用户名</span>
            <span class="info-value">{{ user.username }}</span>
          </div>
          <div class="info-row">
            <span class="info-label">邮箱</span>
            <span class="info-value">{{ user.email }}</span>
          </div>
          <div class="info-row">
            <span class="info-label">用户 ID</span>
            <span class="info-value mono">{{ user.id }}</span>
          </div>
          <div class="info-row">
            <span class="info-label">角色</span>
            <span class="info-value">
              <span v-if="user.isAdmin || user.is_admin" class="badge badge-admin">管理员</span>
              <span v-else class="badge badge-user">普通用户</span>
            </span>
          </div>
          <div class="info-row" v-if="user.createdAt || user.created_at">
            <span class="info-label">注册时间</span>
            <span class="info-value">{{ formatDate(user.createdAt || user.created_at) }}</span>
          </div>
          <div class="info-row" v-if="user.lastLogin || user.last_login">
            <span class="info-label">最近登录</span>
            <span class="info-value">{{ formatDate(user.lastLogin || user.last_login) }}</span>
          </div>
        </div>
        <div v-else class="card-empty">无法加载用户信息</div>
      </section>

      <section class="profile-card profile-password-card">
        <h2 class="card-heading">
          <el-icon class="card-icon"><Lock /></el-icon>
          修改密码
        </h2>
        <el-form
          ref="passwordFormRef"
          :model="passwordForm"
          :rules="passwordRules"
          label-position="top"
          class="password-form"
          @submit.prevent="submitPassword"
        >
          <el-form-item label="当前密码" prop="currentPassword">
            <el-input
              v-model="passwordForm.currentPassword"
              type="password"
              placeholder="请输入当前密码"
              size="large"
              show-password
              autocomplete="current-password"
              :prefix-icon="Lock"
            />
          </el-form-item>
          <el-form-item label="新密码" prop="newPassword">
            <el-input
              v-model="passwordForm.newPassword"
              type="password"
              placeholder="至少 6 位字符"
              size="large"
              show-password
              autocomplete="new-password"
              :prefix-icon="Lock"
            />
          </el-form-item>
          <el-form-item label="确认新密码" prop="confirmPassword">
            <el-input
              v-model="passwordForm.confirmPassword"
              type="password"
              placeholder="再次输入新密码"
              size="large"
              show-password
              autocomplete="new-password"
              :prefix-icon="Lock"
            />
          </el-form-item>
          <button
            type="submit"
            class="submit-password-btn"
            :disabled="passwordLoading"
          >
            {{ passwordLoading ? '提交中...' : '确认修改密码' }}
          </button>
        </el-form>
      </section>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useStore } from 'vuex'
import { ElMessage } from 'element-plus'
import { ArrowLeft, User, Lock } from '@element-plus/icons-vue'
import authAPI from '@/api/auth'
import dayjs from 'dayjs'

const router = useRouter()
const store = useStore()

const user = ref(null)
const loading = ref(true)
const passwordFormRef = ref(null)
const passwordLoading = ref(false)
const passwordForm = ref({
  currentPassword: '',
  newPassword: '',
  confirmPassword: ''
})

const validateConfirm = (rule, value, callback) => {
  if (value !== passwordForm.value.newPassword) {
    callback(new Error('两次输入的新密码不一致'))
  } else {
    callback()
  }
}

const passwordRules = {
  currentPassword: [
    { required: true, message: '请输入当前密码', trigger: 'blur' }
  ],
  newPassword: [
    { required: true, message: '请输入新密码', trigger: 'blur' },
    { min: 6, message: '新密码至少 6 位', trigger: 'blur' }
  ],
  confirmPassword: [
    { required: true, message: '请再次输入新密码', trigger: 'blur' },
    { validator: validateConfirm, trigger: 'blur' }
  ]
}

function formatDate(val) {
  if (!val) return '—'
  const raw = String(val).trim()
  if (/^\d+$/.test(raw)) {
    const n = Number(raw)
    const ts = n < 1e12 ? n * 1000 : n
    return dayjs(ts).format('YYYY-MM-DD HH:mm')
  }
  const d = dayjs(raw)
  return d.isValid() ? d.format('YYYY-MM-DD HH:mm') : raw
}

onMounted(async () => {
  loading.value = true
  try {
    const res = await authAPI.getUserInfo()
    user.value = res.data?.user ?? store.state.user ?? null
    if (!user.value) {
      user.value = store.state.user
    }
  } catch (e) {
    console.error('获取用户信息失败:', e)
  } finally {
    loading.value = false
  }
})

async function submitPassword() {
  if (!passwordFormRef.value) return
  const valid = await passwordFormRef.value.validate().catch(() => false)
  if (!valid) return
  passwordLoading.value = true
  try {
    await authAPI.changePassword(passwordForm.value.currentPassword, passwordForm.value.newPassword)
    ElMessage.success('密码已更新，请使用新密码登录')
    passwordFormRef.value.resetFields()
    passwordForm.value = { currentPassword: '', newPassword: '', confirmPassword: '' }
  } catch (e) {
    console.error('修改密码失败:', e)
  } finally {
    passwordLoading.value = false
  }
}
</script>

<style scoped>
.profile-page {
  min-height: 100vh;
  background: linear-gradient(180deg, #f8fafc 0%, #f1f5f9 100%);
  padding: 2rem 1.5rem 3rem;
}

.profile-header {
  max-width: 640px;
  margin: 0 auto 2rem;
}

.back-btn {
  display: inline-flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.5rem 0;
  color: #475569;
  font-size: 0.9rem;
  background: none;
  border: none;
  cursor: pointer;
  transition: color 0.2s ease;
}
.back-btn:hover {
  color: #0f172a;
}
.back-btn:focus-visible {
  outline: 2px solid #2563eb;
  outline-offset: 2px;
}

.profile-title {
  font-size: 1.75rem;
  font-weight: 700;
  color: #0f172a;
  margin: 0.5rem 0 0.25rem;
  letter-spacing: -0.02em;
}

.profile-desc {
  font-size: 0.95rem;
  color: #64748b;
  margin: 0;
}

.profile-content {
  max-width: 640px;
  margin: 0 auto;
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
}

.profile-card {
  background: #fff;
  border-radius: 16px;
  padding: 1.5rem 1.75rem;
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.06);
  border: 1px solid rgba(0, 0, 0, 0.06);
}

.card-heading {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  font-size: 1.1rem;
  font-weight: 600;
  color: #18181b;
  margin: 0 0 1.25rem;
}

.card-icon {
  font-size: 1.25rem;
  color: #2563eb;
}

.card-loading,
.card-empty {
  color: #64748b;
  font-size: 0.9rem;
}

.info-grid {
  display: flex;
  flex-direction: column;
  gap: 1rem;
}

.info-row {
  display: grid;
  grid-template-columns: 100px 1fr;
  align-items: center;
  gap: 1rem;
  padding: 0.5rem 0;
  border-bottom: 1px solid #f1f5f9;
}
.info-row:last-child {
  border-bottom: none;
}

.info-label {
  font-size: 0.875rem;
  color: #64748b;
}

.info-value {
  font-size: 0.95rem;
  color: #0f172a;
  font-weight: 500;
}

.info-value.mono {
  font-family: ui-monospace, monospace;
  font-size: 0.875rem;
}

.badge {
  display: inline-block;
  padding: 0.2rem 0.6rem;
  border-radius: 999px;
  font-size: 0.75rem;
  font-weight: 600;
}
.badge-admin {
  background: #dbeafe;
  color: #1d4ed8;
}
.badge-user {
  background: #f1f5f9;
  color: #475569;
}

.password-form :deep(.el-form-item) {
  margin-bottom: 1.25rem;
}
.password-form :deep(.el-form-item__label) {
  font-weight: 500;
  color: #374151;
}

.submit-password-btn {
  width: 100%;
  padding: 0.75rem 1.25rem;
  font-size: 1rem;
  font-weight: 600;
  color: #fff;
  background: #2563eb;
  border: none;
  border-radius: 10px;
  cursor: pointer;
  transition: background 0.2s ease;
}
.submit-password-btn:hover:not(:disabled) {
  background: #1d4ed8;
}
.submit-password-btn:focus-visible {
  outline: 2px solid #2563eb;
  outline-offset: 2px;
}
.submit-password-btn:disabled {
  opacity: 0.7;
  cursor: not-allowed;
}

@media (max-width: 640px) {
  .profile-page {
    padding: 1.25rem 1rem 2rem;
  }
  .info-row {
    grid-template-columns: 1fr;
    gap: 0.25rem;
  }
}
</style>
