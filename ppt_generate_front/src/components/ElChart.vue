<template>
  <div ref="chartRef" class="el-chart" :style="chartStyle"></div>
</template>

<script setup>
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import * as echarts from 'echarts'

const props = defineProps({
  option: {
    type: Object,
    required: true
  },
  height: {
    type: [Number, String],
    default: 260
  },
  width: {
    type: [Number, String],
    default: '100%'
  },
  autoresize: {
    type: Boolean,
    default: true
  }
})

const chartRef = ref(null)
let chartInstance = null
let resizeObserver = null
let initRetries = 0
const maxInitRetries = 12

const chartStyle = computed(() => ({
  height: typeof props.height === 'number' ? `${props.height}px` : props.height,
  width: typeof props.width === 'number' ? `${props.width}px` : props.width
}))

const ensureChart = () => {
  if (!chartRef.value) {
    return
  }
  const { clientWidth, clientHeight } = chartRef.value
  if ((!clientWidth || !clientHeight) && initRetries < maxInitRetries) {
    initRetries += 1
    requestAnimationFrame(ensureChart)
    return
  }
  if (!clientWidth || !clientHeight) {
    return
  }
  if (!chartInstance) {
    chartInstance = echarts.init(chartRef.value)
  }
  chartInstance.setOption(props.option, true)
}

const resizeChart = () => {
  if (chartInstance) {
    chartInstance.resize()
  }
}

const getDataURL = (type = 'png') => {
  if (!chartInstance) {
    return ''
  }
  return chartInstance.getDataURL({
    type,
    pixelRatio: 2,
    backgroundColor: '#ffffff'
  })
}

const getInstance = () => chartInstance
const refresh = () => {
  initRetries = 0
  ensureChart()
}

defineExpose({ getDataURL, getInstance, refresh })

onMounted(() => {
  ensureChart()
  if (props.autoresize) {
    window.addEventListener('resize', resizeChart)
    if (window.ResizeObserver && chartRef.value) {
      resizeObserver = new ResizeObserver(() => resizeChart())
      resizeObserver.observe(chartRef.value)
    }
  }
})

watch(
  () => props.option,
  () => {
    if (!chartInstance) {
      ensureChart()
      return
    }
    chartInstance.setOption(props.option, true)
  },
  { deep: true }
)

watch(
  () => [props.height, props.width],
  () => {
    nextTick(() => resizeChart())
  }
)

onBeforeUnmount(() => {
  if (props.autoresize) {
    window.removeEventListener('resize', resizeChart)
    if (resizeObserver && chartRef.value) {
      resizeObserver.disconnect()
    }
  }
  if (chartInstance) {
    chartInstance.dispose()
    chartInstance = null
  }
})
</script>

<style scoped>
.el-chart {
  min-height: 160px;
}
</style>
