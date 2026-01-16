<template>
  <div class="register-container">
    <el-card class="register-form" :body-style="{ padding: '30px' }">
      <div class="logo-section">
        <el-icon size="48" class="logo-icon">
          <UserFilled />
        </el-icon>
        <h2 class="title">创建账户</h2>
      </div>

      <el-form :model="registerForm" :rules="rules" ref="registerFormRef" label-width="0px">
        <el-form-item prop="username">
          <el-input 
            v-model="registerForm.username" 
            placeholder="请输入用户名" 
            size="large"
            prefix-icon="User">
          </el-input>
        </el-form-item>

        <el-form-item prop="email">
          <el-input 
            v-model="registerForm.email" 
            placeholder="请输入邮箱地址" 
            size="large"
            prefix-icon="Message">
          </el-input>
        </el-form-item>

        <el-form-item prop="password">
          <el-input 
            v-model="registerForm.password" 
            type="password" 
            placeholder="请输入密码" 
            size="large"
            prefix-icon="Lock"
            show-password>
          </el-input>
        </el-form-item>

        <el-form-item prop="confirmPassword">
          <el-input 
            v-model="registerForm.confirmPassword" 
            type="password" 
            placeholder="请确认密码" 
            size="large"
            prefix-icon="Unlock">
          </el-input>
        </el-form-item>

        <el-form-item>
          <el-button 
            type="primary" 
            size="large" 
            @click="handleRegister" 
            :loading="loading"
            style="width: 100%">
            注册
          </el-button>
        </el-form-item>
      </el-form>

      <div class="login-link">
        <span>已有账户？</span>
        <el-link type="primary" @click="$router.push('/login')">立即登录</el-link>
      </div>
    </el-card>
  </div>
</template>

<script setup>
import { ref, reactive } from 'vue'
import { useRouter } from 'vue-router'
import { useStore } from 'vuex'
import { ElMessage } from 'element-plus'
import { UserFilled, User, Message, Lock, Unlock } from '@element-plus/icons-vue'

const router = useRouter()
const store = useStore()

const loading = ref(false)

const registerForm = reactive({
  username: '',
  email: '',
  password: '',
  confirmPassword: ''
})

const rules = {
  username: [
    { required: true, message: '请输入用户名', trigger: 'blur' },
    { min: 3, max: 15, message: '用户名长度应在3-15个字符之间', trigger: 'blur' }
  ],
  email: [
    { required: true, message: '请输入邮箱地址', trigger: 'blur' },
    { type: 'email', message: '请输入正确的邮箱地址', trigger: 'blur' }
  ],
  password: [
    { required: true, message: '请输入密码', trigger: 'blur' },
    { min: 6, message: '密码长度不能少于6个字符', trigger: 'blur' }
  ],
  confirmPassword: [
    { required: true, message: '请确认密码', trigger: 'blur' },
    { 
      validator: (rule, value, callback) => {
        if (value !== registerForm.password) {
          callback(new Error('两次输入的密码不一致'))
        } else {
          callback()
        }
      }, 
      trigger: 'blur' 
    }
  ]
}

const registerFormRef = ref(null)

const handleRegister = async () => {
  if (!registerFormRef.value) return
  
  await registerFormRef.value.validate(async (valid) => {
    if (valid) {
      loading.value = true
      try {
        await store.dispatch('register', registerForm)
        ElMessage.success('注册成功！')
        router.push('/login')
      } catch (error) {
        ElMessage.error(error.message || '注册失败')
      } finally {
        loading.value = false
      }
    } else {
      ElMessage.warning('请填写正确的注册信息')
    }
  })
}
</script>

<style scoped>
.register-container {
  display: flex;
  justify-content: center;
  align-items: center;
  min-height: 100vh;
  background: linear-gradient(135deg, #72c2f8, #4a90e2, #0a4db0);
  padding: 20px;
}

.register-form {
  width: 100%;
  max-width: 420px;
  border-radius: 12px;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.1);
}

.logo-section {
  text-align: center;
  margin-bottom: 30px;
}

.logo-icon {
  color: #409EFF;
  margin-bottom: 15px;
}

.title {
  font-size: 22px;
  font-weight: 600;
  color: #303133;
  margin: 0;
}

.login-link {
  text-align: center;
  margin-top: 20px;
  font-size: 14px;
  color: #606266;
}

.login-link .el-link {
  vertical-align: baseline;
}
</style>
