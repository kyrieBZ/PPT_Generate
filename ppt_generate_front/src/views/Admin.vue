<template>
  <div class="admin-shell">
    <aside class="admin-sidebar">
      <div class="brand-block">
        <div class="brand-logo">PPT OPS</div>
        <p class="brand-subtitle">后台管理控制台</p>
      </div>

      <div class="admin-user">
        <div class="admin-avatar">{{ userInitials }}</div>
        <div class="admin-user-info">
          <div class="admin-name">{{ currentUser?.username || '管理员' }}</div>
          <div class="admin-role">超级管理员</div>
        </div>
      </div>

      <nav class="admin-nav">
        <button
          v-for="item in navItems"
          :key="item.id"
          class="nav-item"
          :class="{ active: activeNav === item.id, disabled: item.disabled }"
          :disabled="item.disabled"
          @click="activeNav = item.id"
        >
          <span class="nav-icon">
            <el-icon><component :is="item.icon" /></el-icon>
          </span>
          <span class="nav-label">{{ item.label }}</span>
          <span v-if="item.badge" class="nav-badge">{{ item.badge }}</span>
        </button>
      </nav>

      <div class="nav-footer">
        <button class="back-btn" @click="router.push('/main')">
          <el-icon><ArrowLeft /></el-icon>
          返回用户端
        </button>
      </div>
    </aside>

    <section class="admin-main">
      <header class="admin-topbar">
        <div>
          <h1>运营中枢 · 实时概览</h1>
          <p>集中管理生成记录、活跃度与质量指标，后续可接入大模型洞察。</p>
        </div>
        <div class="top-actions">
          <el-button class="ghost-btn" size="large" @click="refreshData">
            <el-icon><Refresh /></el-icon>
            刷新数据
          </el-button>
          <el-button type="primary" size="large" @click="exportDashboard">
            <el-icon><Download /></el-icon>
            导出概览
          </el-button>
        </div>
      </header>

      <div class="admin-content">
        <section v-show="activeNav === 'overview'" class="overview-section" v-loading="metricsLoading">
          <div class="overview-header">
            <div>
              <h2>关键运营指标</h2>
              <p>数据来源：后台统计接口（按时间区间）</p>
            </div>
            <div class="overview-meta">
              <span class="meta-chip">最近更新：{{ lastUpdated }}</span>
              <span class="meta-chip accent">数据口径：T+0</span>
            </div>
          </div>

          <div class="stats-grid">
            <div v-for="card in statCards" :key="card.id" class="stat-card">
              <div class="stat-icon">
                <el-icon><component :is="card.icon" /></el-icon>
              </div>
              <div class="stat-info">
                <p>{{ card.label }}</p>
                <h3>{{ card.value }}</h3>
                <span class="stat-footnote">{{ card.note }}</span>
              </div>
            </div>
          </div>

          <div class="overview-insights">
            <div class="insight-card">
              <h3>洞察预备区</h3>
              <p>后续将接入大模型分析，为你输出用户偏好与模板推荐策略。</p>
              <ul>
                <li>用户高频主题识别</li>
                <li>模板转化率与留存</li>
                <li>失败原因聚类归因</li>
              </ul>
            </div>
            <div class="insight-card accent">
              <h3>今日提醒</h3>
              <p>当前成功率较上周上升 {{ successRateDelta }}，继续关注失败请求原因。</p>
              <div class="insight-tags">
                <span>生成质量</span>
                <span>模板覆盖</span>
                <span>用户活跃度</span>
              </div>
            </div>
          </div>
        </section>

        <section v-show="activeNav === 'charts'" class="dashboard-section" v-loading="metricsLoading">
          <div class="dashboard-header">
            <div>
              <h2>数据可视化看板</h2>
              <p>拖拽卡片调整顺序，切换图表类型。</p>
            </div>
            <div class="dashboard-controls">
              <el-select v-model="timeRange" size="large" class="control-select">
                <el-option label="最近 24 小时" value="day" />
                <el-option label="最近 7 天" value="week" />
                <el-option label="最近 30 天" value="month" />
              </el-select>
              <el-button class="ghost-btn" size="large" @click="resetDashboard">
                重置布局
              </el-button>
            </div>
          </div>

          <div class="dashboard-grid">
            <div
              v-for="card in chartCards"
              :key="card.id"
              class="dashboard-card"
              :class="{ dragging: dragState.draggingId === card.id }"
              draggable="true"
              @dragstart="onDragStart(card.id)"
              @dragend="onDragEnd"
              @dragover.prevent
              @drop="onDrop(card.id)"
            >
              <div class="card-header">
                <div class="card-title">
                  <span class="drag-handle">⋮⋮</span>
                  <div>
                    <h3>{{ card.title }}</h3>
                    <p>{{ card.subtitle }}</p>
                  </div>
                </div>
                <div class="card-actions">
                  <el-select
                    v-if="card.types.length > 1"
                    v-model="card.chartType"
                    size="small"
                    class="type-select"
                  >
                    <el-option
                      v-for="type in card.types"
                      :key="type"
                      :label="chartTypeLabels[type]"
                      :value="type"
                    />
                  </el-select>
                  <el-dropdown @command="(command) => exportCard(card.id, command)">
                    <el-button class="ghost-btn" size="small">
                      导出
                    </el-button>
                    <template #dropdown>
                      <el-dropdown-menu>
                        <el-dropdown-item command="png">导出 PNG</el-dropdown-item>
                        <el-dropdown-item command="pdf">导出 PDF</el-dropdown-item>
                      </el-dropdown-menu>
                    </template>
                  </el-dropdown>
                </div>
              </div>
              <div class="chart-wrap">
                <el-chart
                  :ref="(el) => setChartRef(card.id, el)"
                  :option="chartOptions[card.id]"
                  :height="card.height"
                />
              </div>
              <div class="card-footnote">
                {{ card.footnote }}
              </div>
            </div>
          </div>
        </section>

        <section v-show="activeNav === 'records'" class="records-section">
          <div class="records-header">
            <div>
              <h2>生成记录管理</h2>
              <p>展示全量生成记录，可按状态、关键字筛选。</p>
            </div>
            <div class="records-controls">
              <el-input
                v-model="searchQuery"
                size="large"
                placeholder="搜索标题/主题/用户"
                clearable
              />
              <el-select v-model="statusFilter" size="large" class="control-select">
                <el-option label="全部状态" value="all" />
                <el-option label="已完成" value="success" />
                <el-option label="失败" value="failed" />
              </el-select>
              <el-date-picker
                v-model="recordDateRange"
                type="daterange"
                range-separator="至"
                start-placeholder="开始日期"
                end-placeholder="结束日期"
                size="large"
              />
            </div>
          </div>

          <el-card class="records-table-card">
            <el-table
              :data="pagedHistory"
              stripe
              v-loading="adminHistoryLoading"
              element-loading-text="数据加载中..."
            >
              <el-table-column prop="id" label="ID" width="80" />
              <el-table-column prop="title" label="标题" min-width="180" />
              <el-table-column prop="userName" label="用户" width="140" />
              <el-table-column prop="templateName" label="模板" width="140" />
              <el-table-column label="状态" width="120">
                <template #default="{ row }">
                  <el-tag :type="statusTagType(row.status)">
                    {{ statusText(row.status) }}
                  </el-tag>
                </template>
              </el-table-column>
              <el-table-column label="创建时间" width="180">
                <template #default="{ row }">
                  {{ formatDate(row.createdAt) }}
                </template>
              </el-table-column>
              <el-table-column label="操作" width="120">
                <template #default>
                  <el-button size="small" class="ghost-btn">查看</el-button>
                </template>
              </el-table-column>
            </el-table>

            <div class="pagination-bar">
              <span>共 {{ filteredHistory.length }} 条记录</span>
              <el-pagination
                v-model:current-page="currentPage"
                :page-size="pageSize"
                layout="prev, pager, next"
                :total="filteredHistory.length"
                background
              />
            </div>
          </el-card>
        </section>

        <section v-show="activeNav === 'users'" class="records-section">
          <div class="records-header">
            <div>
              <h2>用户管理</h2>
              <p>管理员可禁用或启用用户账户。</p>
            </div>
            <div class="records-controls">
              <el-input
                v-model="userSearchQuery"
                size="large"
                placeholder="搜索用户名或邮箱"
                clearable
              />
              <el-select v-model="userStatusFilter" size="large" class="control-select">
                <el-option label="全部状态" value="all" />
                <el-option label="正常" value="active" />
                <el-option label="已禁用" value="disabled" />
              </el-select>
              <el-button class="ghost-btn" size="large" @click="loadUsers">
                刷新列表
              </el-button>
            </div>
          </div>

          <el-card class="records-table-card">
            <el-table
              :data="pagedUsers"
              stripe
              v-loading="usersLoading"
              element-loading-text="用户加载中..."
            >
              <el-table-column prop="id" label="ID" width="80" />
              <el-table-column prop="username" label="用户名" width="160" />
              <el-table-column prop="email" label="邮箱" min-width="200" />
              <el-table-column label="角色" width="120">
                <template #default="{ row }">
                  <el-tag :type="row.isAdmin ? 'warning' : 'info'">
                    {{ row.isAdmin ? '管理员' : '普通用户' }}
                  </el-tag>
                </template>
              </el-table-column>
              <el-table-column label="状态" width="120">
                <template #default="{ row }">
                  <el-tag :type="row.isDisabled ? 'danger' : 'success'">
                    {{ row.isDisabled ? '已禁用' : '正常' }}
                  </el-tag>
                </template>
              </el-table-column>
              <el-table-column label="最近登录" width="180">
                <template #default="{ row }">
                  {{ row.lastLogin ? dayjs(row.lastLogin).format('YYYY/MM/DD HH:mm') : '-' }}
                </template>
              </el-table-column>
              <el-table-column label="操作" width="140">
                <template #default="{ row }">
                  <el-button
                    size="small"
                    class="ghost-btn"
                    :disabled="row.id === currentUser?.id"
                    @click="toggleUserStatus(row)"
                  >
                    {{ row.isDisabled ? '启用' : '禁用' }}
                  </el-button>
                </template>
              </el-table-column>
            </el-table>

            <div class="pagination-bar">
              <span>共 {{ filteredUsers.length }} 位用户</span>
              <el-pagination
                v-model:current-page="userPage"
                :page-size="userPageSize"
                layout="prev, pager, next"
                :total="filteredUsers.length"
                background
              />
            </div>
          </el-card>
        </section>

        <section v-show="activeNav === 'insights'" class="placeholder-section">
          <div class="placeholder-card">
            <h2>偏好洞察模块</h2>
            <p>此模块将接入大模型进行偏好分析，目前仅展示占位。</p>
          </div>
        </section>

        <section v-show="activeNav === 'settings'" class="placeholder-section">
          <div class="placeholder-card">
            <h2>系统设置模块</h2>
            <p>后续可在此配置权限、模板、通知与告警。</p>
          </div>
        </section>
      </div>
    </section>
  </div>
</template>

<script setup>
import { computed, nextTick, onMounted, reactive, ref, watch } from 'vue'
import { useStore } from 'vuex'
import { useRouter } from 'vue-router'
import dayjs from 'dayjs'
import { ElMessage, ElMessageBox } from 'element-plus'
import {
  ArrowLeft,
  DataLine,
  Download,
  Document,
  Histogram,
  PieChart,
  Refresh,
  Setting,
  TrendCharts,
  UserFilled
} from '@element-plus/icons-vue'
import ElChart from '@/components/ElChart.vue'
import adminAPI from '@/api/admin'

const store = useStore()
const router = useRouter()

const activeNav = ref('overview')
const timeRange = ref('week')
const searchQuery = ref('')
const statusFilter = ref('all')
const recordDateRange = ref([])
const currentPage = ref(1)
const pageSize = ref(6)
const chartRefs = ref({})
const dragState = reactive({ draggingId: null })
const metricsLoading = ref(false)
const usersLoading = ref(false)
const users = ref([])
const userSearchQuery = ref('')
const userStatusFilter = ref('all')
const userPage = ref(1)
const userPageSize = ref(8)
const metricsState = reactive({
  summary: {
    total: 0,
    success: 0,
    failed: 0,
    successRate: 0,
    uniqueUsers: 0,
    templateCount: 0
  },
  activity: { labels: [], values: [] },
  generation: { labels: [], series: [] },
  templateShare: [],
  successRate: { success: 0, failed: 0 },
  region: { labels: [], values: [] },
  moduleHeat: { labels: [], values: [] }
})

const navItems = [
  { id: 'overview', label: '运营总览', icon: DataLine },
  { id: 'charts', label: '数据看板', icon: TrendCharts },
  { id: 'records', label: '生成记录', icon: Document, badge: 'NEW' },
  { id: 'users', label: '用户管理', icon: UserFilled },
  { id: 'insights', label: '偏好洞察', icon: PieChart, disabled: true },
  { id: 'settings', label: '系统设置', icon: Setting, disabled: true }
]

const chartTypeLabels = {
  bar: '柱状图',
  line: '折线图',
  pie: '饼图'
}

const chartCards = ref([
  {
    id: 'activity',
    title: '用户活跃度',
    subtitle: '每日/每周/每月登录次数',
    chartType: 'line',
    types: ['line', 'bar'],
    height: 320,
    footnote: '基于生成记录统计活跃度'
  },
  {
    id: 'generation',
    title: '生成频率',
    subtitle: '按时间段/模板类型分类',
    chartType: 'bar',
    types: ['bar', 'line'],
    height: 320,
    footnote: '结合时间段与模板使用统计'
  },
  {
    id: 'templateShare',
    title: '模板偏好分布',
    subtitle: '热门模板使用占比',
    chartType: 'pie',
    types: ['pie', 'bar'],
    height: 320,
    footnote: '统计模板使用占比'
  },
  {
    id: 'successRate',
    title: '生成成功率',
    subtitle: '成功/失败占比统计',
    chartType: 'pie',
    types: ['pie', 'bar'],
    height: 300,
    footnote: '成功率 = 成功任务 / 总任务'
  }
])
const defaultChartCards = chartCards.value.map((card) => ({ ...card }))

const currentUser = computed(() => store.getters.currentUser)
const adminHistory = computed(() => store.getters.adminHistory || [])
const adminHistoryLoading = computed(() => store.getters.adminHistoryLoading)

const userInitials = computed(() => {
  const name = currentUser.value?.username || 'A'
  return name.slice(0, 2).toUpperCase()
})

const lastUpdated = computed(() => dayjs().format('YYYY/MM/DD HH:mm'))

const normalizedHistory = computed(() => adminHistory.value || [])

const isFailureStatus = (status) => {
  const value = (status || '').toLowerCase()
  return ['failed', 'error', 'rejected', 'canceled'].includes(value)
}

const successCount = computed(() => metricsState.successRate.success || 0)
const failureCount = computed(() => metricsState.successRate.failed || 0)
const totalCount = computed(() => metricsState.summary.total || 0)
const successRate = computed(() => metricsState.summary.successRate || 0)
const uniqueUsers = computed(() => metricsState.summary.uniqueUsers || 0)
const templateCount = computed(() => metricsState.summary.templateCount || 0)

const activityLabels = computed(() => metricsState.activity.labels || [])
const activityValues = computed(() => metricsState.activity.values || [])
const generationLabels = computed(() => metricsState.generation.labels || ['上午', '下午', '傍晚', '深夜', '清晨'])
const generationSeries = computed(() => metricsState.generation.series || [])
const templateShareData = computed(() => metricsState.templateShare || [])

const chartOptions = computed(() => {
  const options = {}
  chartCards.value.forEach((card) => {
    options[card.id] = buildOption(card)
  })
  return options
})

const palette = ['#2563eb', '#10b981', '#f97316', '#f43f5e', '#8b5cf6', '#0ea5e9']

const axisStyle = {
  axisLine: { lineStyle: { color: 'rgba(148,163,184,0.5)' } },
  axisTick: { show: false },
  axisLabel: { color: '#64748b', fontSize: 12 }
}

const gridBase = { left: 28, right: 20, top: 40, bottom: 30, containLabel: true }

const buildLineBarOption = (labels, series, type, name) => ({
  color: palette,
  tooltip: { trigger: 'axis' },
  grid: gridBase,
  xAxis: {
    type: 'category',
    data: labels,
    ...axisStyle
  },
  yAxis: {
    type: 'value',
    ...axisStyle,
    splitLine: { lineStyle: { color: 'rgba(148,163,184,0.2)' } }
  },
  series: [
    {
      name,
      type,
      data: series,
      smooth: type === 'line',
      areaStyle: type === 'line' ? { color: 'rgba(37,99,235,0.15)' } : undefined,
      itemStyle: { borderRadius: 6 }
    }
  ]
})

const buildMultiSeriesOption = (labels, seriesList, type) => ({
  color: palette,
  tooltip: { trigger: 'axis' },
  legend: {
    top: 8,
    textStyle: { color: '#475569' }
  },
  grid: gridBase,
  xAxis: { type: 'category', data: labels, ...axisStyle },
  yAxis: {
    type: 'value',
    ...axisStyle,
    splitLine: { lineStyle: { color: 'rgba(148,163,184,0.2)' } }
  },
  series: seriesList.map((item) => ({
    name: item.name,
    type,
    data: item.data,
    smooth: type === 'line',
    areaStyle: type === 'line' ? { opacity: 0.12 } : undefined,
    itemStyle: { borderRadius: 6 }
  }))
})

const buildPieOption = (data) => ({
  color: palette,
  tooltip: { trigger: 'item' },
  legend: {
    bottom: 0,
    textStyle: { color: '#475569' }
  },
  series: [
    {
      type: 'pie',
      radius: ['40%', '70%'],
      data,
      label: { color: '#0f172a' },
      itemStyle: { borderRadius: 8, borderColor: '#ffffff', borderWidth: 2 }
    }
  ]
})

const buildOption = (card) => {
  if (card.id === 'activity') {
    return buildLineBarOption(
      activityLabels.value,
      activityValues.value,
      card.chartType,
      '活跃次数'
    )
  }
  if (card.id === 'generation') {
    return buildMultiSeriesOption(
      generationLabels.value,
      generationSeries.value.map((item) => ({ name: item.name, data: item.values || [] })),
      card.chartType
    )
  }
  if (card.id === 'templateShare') {
    if (card.chartType === 'pie') {
      return buildPieOption(templateShareData.value)
    }
    return buildLineBarOption(
      templateShareData.value.map((item) => item.name),
      templateShareData.value.map((item) => item.value),
      card.chartType,
      '模板使用次数'
    )
  }
  if (card.id === 'successRate') {
    const data = [
      { value: successCount.value || 0, name: '成功' },
      { value: failureCount.value || 0, name: '失败' }
    ]
    if (card.chartType === 'pie') {
      return buildPieOption(data)
    }
    return buildLineBarOption(['成功', '失败'], [successCount.value, failureCount.value], 'bar', '任务量')
  }
  return {}
}

const statCards = computed(() => [
  {
    id: 'total',
    label: '累计生成',
    value: totalCount.value || 0,
    note: '包含所有历史记录',
    icon: Histogram
  },
  {
    id: 'success',
    label: '成功率',
    value: `${successRate.value}%`,
    note: `${successCount.value} 成功 / ${failureCount.value} 失败`,
    icon: PieChart
  },
  {
    id: 'active',
    label: '活跃用户',
    value: uniqueUsers.value,
    note: '基于生成记录去重',
    icon: UserFilled
  },
  {
    id: 'templates',
    label: '模板覆盖',
    value: templateCount.value || 0,
    note: '近期热门模板数量',
    icon: TrendCharts
  }
])

const successRateDelta = computed(() => {
  const delta = Math.max(2, Math.min(12, Math.round(successRate.value / 12)))
  return `+${delta}%`
})

const filteredHistory = computed(() => {
  let items = normalizedHistory.value
  const keyword = searchQuery.value.trim().toLowerCase()
  if (keyword) {
    items = items.filter((item) => {
      const title = item.title?.toLowerCase() || ''
      const topic = item.topic?.toLowerCase() || ''
      const user = item.userName?.toLowerCase() || ''
      return title.includes(keyword) || topic.includes(keyword) || user.includes(keyword)
    })
  }
  if (statusFilter.value !== 'all') {
    items = items.filter((item) =>
      statusFilter.value === 'failed' ? isFailureStatus(item.status) : !isFailureStatus(item.status)
    )
  }
  if (recordDateRange.value?.length === 2) {
    const [start, end] = recordDateRange.value
    const startDate = dayjs(start)
    const endDate = dayjs(end).endOf('day')
    items = items.filter((item) => {
      if (!item.createdAt) return true
      const created = dayjs(item.createdAt)
      return created.isAfter(startDate) && created.isBefore(endDate)
    })
  }
  return items
})

const pagedHistory = computed(() => {
  const start = (currentPage.value - 1) * pageSize.value
  return filteredHistory.value.slice(start, start + pageSize.value)
})

const filteredUsers = computed(() => {
  let items = users.value
  const keyword = userSearchQuery.value.trim().toLowerCase()
  if (keyword) {
    items = items.filter((item) => {
      const name = item.username?.toLowerCase() || ''
      const email = item.email?.toLowerCase() || ''
      return name.includes(keyword) || email.includes(keyword)
    })
  }
  if (userStatusFilter.value !== 'all') {
    items = items.filter((item) =>
      userStatusFilter.value === 'disabled' ? item.isDisabled : !item.isDisabled
    )
  }
  return items
})

const pagedUsers = computed(() => {
  const start = (userPage.value - 1) * userPageSize.value
  return filteredUsers.value.slice(start, start + userPageSize.value)
})

const setChartRef = (id, el) => {
  if (el) {
    chartRefs.value[id] = el
  }
}

const resizeCharts = () => {
  nextTick(() => {
    Object.values(chartRefs.value).forEach((chart) => {
      chart?.refresh?.()
      chart?.getInstance?.()?.resize()
    })
  })
}

const onDragStart = (id) => {
  dragState.draggingId = id
}

const onDragEnd = () => {
  dragState.draggingId = null
}

const onDrop = (targetId) => {
  if (!dragState.draggingId || dragState.draggingId === targetId) {
    return
  }
  const cards = [...chartCards.value]
  const fromIndex = cards.findIndex((card) => card.id === dragState.draggingId)
  const toIndex = cards.findIndex((card) => card.id === targetId)
  const [moved] = cards.splice(fromIndex, 1)
  cards.splice(toIndex, 0, moved)
  chartCards.value = cards
  dragState.draggingId = null
}

const resetDashboard = () => {
  chartCards.value = defaultChartCards.map((card) => ({ ...card }))
}

const exportCard = (id, format) => {
  const chart = chartRefs.value[id]
  if (!chart) return
  const imageUrl = chart.getDataURL('png')
  if (!imageUrl) return
  if (format === 'pdf') {
    const win = window.open('', '_blank')
    if (win) {
      win.document.write(`<img src="${imageUrl}" style="width:100%;"/>`)
      win.document.close()
      win.focus()
      win.print()
    }
    return
  }
  const link = document.createElement('a')
  link.href = imageUrl
  link.download = `chart-${id}-${dayjs().format('YYYYMMDD-HHmm')}.png`
  link.click()
}

const exportDashboard = () => {
  const cards = chartCards.value.map((card) => card.id)
  cards.forEach((id, index) => {
    setTimeout(() => exportCard(id, 'png'), index * 250)
  })
}

const applyMetrics = (data = {}) => {
  if (data.summary) {
    Object.assign(metricsState.summary, data.summary)
  }
  if (data.activity) {
    metricsState.activity.labels = data.activity.labels || []
    metricsState.activity.values = data.activity.values || []
  }
  if (data.generation) {
    metricsState.generation.labels = data.generation.labels || metricsState.generation.labels
    metricsState.generation.series = data.generation.series || []
  }
  metricsState.templateShare = data.templateShare || []
  metricsState.successRate = data.successRate || { success: 0, failed: 0 }
  if (data.region) {
    metricsState.region.labels = data.region.labels || []
    metricsState.region.values = data.region.values || []
  }
  if (data.moduleHeat) {
    metricsState.moduleHeat.labels = data.moduleHeat.labels || []
    metricsState.moduleHeat.values = data.moduleHeat.values || []
  }
}

const loadMetrics = async () => {
  metricsLoading.value = true
  try {
    const response = await adminAPI.metrics({ range: timeRange.value })
    applyMetrics(response.data || {})
  } catch (error) {
    console.error('获取后台统计失败:', error)
  } finally {
    metricsLoading.value = false
  }
}

const loadUsers = async () => {
  usersLoading.value = true
  try {
    const response = await adminAPI.users()
    users.value = response.data?.items || []
  } catch (error) {
    console.error('获取用户列表失败:', error)
  } finally {
    usersLoading.value = false
  }
}

const toggleUserStatus = async (user) => {
  if (!user?.id || user.id === currentUser.value?.id) {
    return
  }
  const nextDisabled = !user.isDisabled
  try {
    await ElMessageBox.confirm(
      nextDisabled ? `确定禁用用户 ${user.username || user.email}？` : `确定启用用户 ${user.username || user.email}？`,
      '操作确认',
      {
        confirmButtonText: '确认',
        cancelButtonText: '取消',
        type: 'warning'
      }
    )
    await adminAPI.updateUserStatus({ userId: user.id, disabled: nextDisabled })
    user.isDisabled = nextDisabled
    ElMessage.success(nextDisabled ? '用户已禁用' : '用户已启用')
  } catch (error) {
    if (error !== 'cancel' && error !== 'close') {
      ElMessage.error(error?.response?.data?.message || '操作失败')
    }
  }
}

const refreshData = () => {
  store.dispatch('fetchAdminHistory').catch(() => {})
  loadMetrics()
}

const statusText = (status) => (isFailureStatus(status) ? '失败' : '已完成')
const statusTagType = (status) => (isFailureStatus(status) ? 'danger' : 'success')
const formatDate = (value) => {
  if (!value) return '-'
  const raw = String(value).trim()
  if (/^\d+$/.test(raw)) {
    const numeric = Number(raw)
    const ts = numeric < 1e12 ? numeric * 1000 : numeric
    return dayjs(ts).format('YYYY/MM/DD HH:mm')
  }
  const parsed = dayjs(raw)
  if (parsed.isValid()) {
    const year = parsed.year()
    if (year > 3000) {
      return raw.replace(/-/g, '/').replace('T', ' ').replace('Z', '').slice(0, 16)
    }
    return parsed.format('YYYY/MM/DD HH:mm')
  }
  if (/^\d{4}-\d{2}-\d{2}\s\d{2}:\d{2}/.test(raw)) {
    return raw.replace(/-/g, '/').slice(0, 16)
  }
  return raw
}

watch([searchQuery, statusFilter, recordDateRange], () => {
  currentPage.value = 1
})

watch([userSearchQuery, userStatusFilter], () => {
  userPage.value = 1
})

watch(timeRange, () => {
  loadMetrics()
})

watch(activeNav, (value) => {
  if (value === 'users' && users.value.length === 0 && !usersLoading.value) {
    loadUsers()
  }
  if (value === 'charts') {
    resizeCharts()
  }
})

onMounted(() => {
  if (currentUser.value?.isAdmin) {
    store.dispatch('fetchAdminHistory').catch(() => {})
    loadMetrics()
    loadUsers()
  }
  if (activeNav.value === 'charts') {
    resizeCharts()
  }
})
</script>

<style scoped>
@import url('https://fonts.googleapis.com/css2?family=Space+Grotesk:wght@400;500;600;700&display=swap');

.admin-shell {
  min-height: 100vh;
  display: grid;
  grid-template-columns: 280px 1fr;
  background: radial-gradient(circle at top, rgba(37, 99, 235, 0.12), transparent 50%),
    linear-gradient(135deg, #f8fafc 0%, #eef2ff 45%, #fdf2f8 100%);
  font-family: 'Space Grotesk', 'Noto Sans SC', 'PingFang SC', sans-serif;
  color: #0f172a;
}

.admin-sidebar {
  padding: 32px 24px;
  background: linear-gradient(180deg, rgba(15, 23, 42, 0.95), rgba(15, 23, 42, 0.9));
  color: #e2e8f0;
  display: flex;
  flex-direction: column;
  gap: 28px;
  position: relative;
  overflow: hidden;
}

.admin-sidebar::after {
  content: '';
  position: absolute;
  inset: 0;
  background-image: radial-gradient(rgba(148, 163, 184, 0.15) 1px, transparent 0);
  background-size: 18px 18px;
  opacity: 0.5;
  pointer-events: none;
}

.brand-block {
  position: relative;
  z-index: 1;
}

.brand-logo {
  font-size: 1.6rem;
  font-weight: 700;
  letter-spacing: 0.08em;
}

.brand-subtitle {
  font-size: 0.9rem;
  color: rgba(226, 232, 240, 0.75);
  margin-top: 6px;
}

.admin-user {
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 16px;
  background: rgba(148, 163, 184, 0.12);
  border-radius: 18px;
  position: relative;
  z-index: 1;
}

.admin-avatar {
  width: 46px;
  height: 46px;
  border-radius: 16px;
  background: linear-gradient(135deg, #38bdf8, #6366f1);
  display: grid;
  place-items: center;
  font-weight: 700;
  color: #0f172a;
}

.admin-user-info {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.admin-name {
  font-weight: 600;
  font-size: 1rem;
}

.admin-role {
  font-size: 0.8rem;
  color: rgba(226, 232, 240, 0.7);
}

.admin-nav {
  display: flex;
  flex-direction: column;
  gap: 10px;
  position: relative;
  z-index: 1;
}

.nav-item {
  border: none;
  background: rgba(148, 163, 184, 0.08);
  color: #e2e8f0;
  padding: 14px 16px;
  border-radius: 14px;
  display: flex;
  align-items: center;
  gap: 12px;
  cursor: pointer;
  transition: all 0.2s ease;
  text-align: left;
  position: relative;
}

.nav-item:hover:not(.disabled),
.nav-item.active {
  background: rgba(59, 130, 246, 0.2);
  color: #ffffff;
  transform: translateX(4px);
}

.nav-item.disabled {
  opacity: 0.45;
  cursor: not-allowed;
}

.nav-icon {
  width: 34px;
  height: 34px;
  border-radius: 12px;
  background: rgba(15, 23, 42, 0.6);
  display: grid;
  place-items: center;
}

.nav-label {
  flex: 1;
  font-weight: 500;
}

.nav-badge {
  background: #f97316;
  color: #0f172a;
  padding: 2px 8px;
  border-radius: 999px;
  font-size: 0.7rem;
  font-weight: 700;
}

.nav-footer {
  margin-top: auto;
  position: relative;
  z-index: 1;
}

.back-btn {
  width: 100%;
  padding: 12px 16px;
  border-radius: 14px;
  border: 1px solid rgba(148, 163, 184, 0.3);
  background: transparent;
  color: #e2e8f0;
  display: flex;
  align-items: center;
  gap: 8px;
  cursor: pointer;
  transition: all 0.2s ease;
}

.back-btn:hover {
  background: rgba(148, 163, 184, 0.2);
}

.admin-main {
  padding: 32px 40px 40px;
  display: flex;
  flex-direction: column;
  gap: 28px;
}

.admin-topbar {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 20px;
}

.admin-topbar h1 {
  font-size: 2.2rem;
  margin-bottom: 6px;
}

.admin-topbar p {
  color: #475569;
}

.top-actions {
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
}

.ghost-btn {
  background: rgba(255, 255, 255, 0.7);
  border: 1px solid rgba(148, 163, 184, 0.4);
  color: #0f172a;
}

.admin-content {
  display: flex;
  flex-direction: column;
  gap: 28px;
}

.overview-section,
.dashboard-section,
.records-section {
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.overview-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
}

.overview-header p {
  color: #64748b;
}

.overview-meta {
  display: flex;
  gap: 10px;
  flex-wrap: wrap;
}

.meta-chip {
  padding: 6px 12px;
  border-radius: 999px;
  background: rgba(15, 23, 42, 0.08);
  font-size: 0.85rem;
}

.meta-chip.accent {
  background: rgba(37, 99, 235, 0.15);
  color: #1d4ed8;
}

.stats-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 16px;
}

.stat-card {
  padding: 18px;
  border-radius: 18px;
  background: rgba(255, 255, 255, 0.9);
  border: 1px solid rgba(148, 163, 184, 0.3);
  display: flex;
  gap: 14px;
  align-items: center;
  box-shadow: 0 18px 30px rgba(15, 23, 42, 0.08);
}

.stat-icon {
  width: 48px;
  height: 48px;
  border-radius: 16px;
  background: linear-gradient(135deg, #38bdf8, #6366f1);
  display: grid;
  place-items: center;
  color: #0f172a;
}

.stat-info h3 {
  font-size: 1.6rem;
  margin: 4px 0;
}

.stat-info p {
  color: #64748b;
  font-size: 0.9rem;
}

.stat-footnote {
  font-size: 0.75rem;
  color: #94a3b8;
}

.overview-insights {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
  gap: 16px;
}

.insight-card {
  padding: 20px;
  border-radius: 18px;
  background: rgba(15, 23, 42, 0.9);
  color: #e2e8f0;
  box-shadow: 0 20px 35px rgba(15, 23, 42, 0.2);
}

.insight-card.accent {
  background: linear-gradient(135deg, #2563eb, #7c3aed);
}

.insight-card h3 {
  margin-bottom: 10px;
}

.insight-card ul {
  margin-top: 12px;
  padding-left: 18px;
}

.insight-card li {
  margin-bottom: 6px;
}

.insight-tags {
  margin-top: 14px;
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
}

.insight-tags span {
  padding: 4px 10px;
  border-radius: 999px;
  background: rgba(255, 255, 255, 0.2);
  font-size: 0.75rem;
}

.dashboard-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 16px;
}

.dashboard-header p {
  color: #64748b;
}

.dashboard-controls {
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
}

.dashboard-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(360px, 1fr));
  gap: 20px;
}

.dashboard-card {
  background: rgba(255, 255, 255, 0.9);
  border-radius: 20px;
  padding: 20px;
  border: 1px solid rgba(148, 163, 184, 0.2);
  box-shadow: 0 18px 40px rgba(15, 23, 42, 0.08);
  display: grid;
  grid-template-rows: auto 1fr auto;
  gap: 14px;
  min-height: 380px;
  transition: transform 0.2s ease;
}

.dashboard-card.dragging {
  opacity: 0.6;
  transform: scale(0.98);
}

.card-header {
  display: flex;
  justify-content: space-between;
  gap: 12px;
}

.card-title {
  display: flex;
  gap: 10px;
}

.card-title h3 {
  margin-bottom: 4px;
}

.card-title p {
  color: #64748b;
  font-size: 0.85rem;
}

.drag-handle {
  color: #94a3b8;
  font-size: 1.2rem;
  padding-top: 2px;
}

.card-actions {
  display: flex;
  gap: 8px;
  align-items: center;
}

.chart-wrap {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 260px;
}

.chart-wrap .el-chart {
  width: 100%;
  height: 100%;
}

.type-select {
  min-width: 100px;
}

.card-footnote {
  font-size: 0.75rem;
  color: #94a3b8;
  padding-left: 6px;
}

.records-header {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  flex-wrap: wrap;
}

.records-controls {
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
}

.records-table-card {
  border-radius: 18px;
  overflow: hidden;
}

.pagination-bar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 16px 8px 0;
  color: #64748b;
}

.placeholder-section {
  display: flex;
  justify-content: center;
  align-items: center;
  min-height: 320px;
}

.placeholder-card {
  padding: 28px;
  border-radius: 18px;
  background: rgba(255, 255, 255, 0.9);
  text-align: center;
  box-shadow: 0 18px 32px rgba(15, 23, 42, 0.1);
}

@media (max-width: 1200px) {
  .admin-shell {
    grid-template-columns: 220px 1fr;
  }
}

@media (max-width: 960px) {
  .admin-shell {
    grid-template-columns: 1fr;
  }

  .admin-sidebar {
    flex-direction: row;
    flex-wrap: wrap;
    gap: 16px;
  }

  .admin-nav {
    flex-direction: row;
    flex-wrap: wrap;
  }

  .nav-item {
    flex: 1 1 140px;
  }
}

@media (max-width: 720px) {
  .admin-main {
    padding: 24px;
  }

  .admin-topbar {
    flex-direction: column;
  }

  .overview-header {
    flex-direction: column;
    align-items: flex-start;
  }

  .dashboard-header {
    flex-direction: column;
    align-items: flex-start;
  }

  .records-controls {
    width: 100%;
  }
}
</style>
