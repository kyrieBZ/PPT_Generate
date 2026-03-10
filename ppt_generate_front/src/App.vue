<template>
  <router-view />
</template>

<script setup>
import { onMounted } from 'vue'
import { useStore } from 'vuex'

const store = useStore()

onMounted(() => {
  store.dispatch('bootstrapSession').catch((error) => {
    console.error('会话初始化失败:', error)
  })
})
</script>

<style>
* {
  margin: 0;
  padding: 0;
  box-sizing: border-box;
}

body {
  font-family: 'Plus Jakarta Sans', 'Noto Sans SC', -apple-system, BlinkMacSystemFont, sans-serif;
  line-height: var(--line-height-body, 1.55);
  color: var(--color-text);
  background-color: var(--color-bg-page);
}


#app {
  min-height: 100vh;
}

button {
  font-family: inherit;
}

input, textarea, select {
  font-family: inherit;
  font-size: inherit;
}

a {
  color: inherit;
  text-decoration: none;
}
</style>
