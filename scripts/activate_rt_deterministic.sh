#!/bin/bash
# Script to activate RT deterministic threading mode
# This script applies the remaining patches to complete the activation

set -e

echo "🚀 Activating RT Deterministic Threading Mode..."
echo ""

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if we're in the right directory
if [ ! -f "src/synthesis/additive/synth_additive.c" ]; then
    echo "❌ Error: Must be run from project root directory"
    exit 1
fi

echo "${YELLOW}Step 1/4: Modifying synth_persistent_worker_thread()...${NC}"

# Create backup
cp src/synthesis/additive/synth_additive_threading.c src/synthesis/additive/synth_additive_threading.c.bak

# Apply patch to worker thread loop
cat > /tmp/worker_patch.txt << 'EOF'
void *synth_persistent_worker_thread(void *arg) {
  synth_thread_worker_t *worker = (synth_thread_worker_t *)arg;

  while (!synth_pool_shutdown) {
    if (g_use_barriers) {
      // ✅ PHASE 2: Deterministic barrier synchronization
      // Wait at start barrier (all workers + main thread)
      synth_barrier_wait(&g_worker_start_barrier);
      
      if (synth_pool_shutdown) break;
      
      // Process work
      synth_process_worker_range(worker);
      
      // Wait at end barrier (synchronize completion)
      synth_barrier_wait(&g_worker_end_barrier);
    } else {
      // Fallback to condition variables
      pthread_mutex_lock(&worker->work_mutex);
      while (!worker->work_ready && !synth_pool_shutdown) {
        pthread_cond_wait(&worker->work_cond, &worker->work_mutex);
      }
      pthread_mutex_unlock(&worker->work_mutex);

      if (synth_pool_shutdown) break;

      synth_process_worker_range(worker);

      pthread_mutex_lock(&worker->work_mutex);
      worker->work_done = 1;
      worker->work_ready = 0;
      pthread_mutex_unlock(&worker->work_mutex);
    }
  }

  return NULL;
}
EOF

echo "${GREEN}✓ Worker thread loop patch created${NC}"

echo ""
echo "${YELLOW}Step 2/4: Adding cleanup to synth_shutdown_thread_pool()...${NC}"

cat > /tmp/shutdown_patch.txt << 'EOF'
  // Cleanup barriers if they were initialized
  if (g_use_barriers) {
    synth_cleanup_barriers();
    log_info("SYNTH", "Barrier synchronization cleaned up");
  }

  if (g_sp3ctra_config.stereo_mode_enabled) {
    // Cleanup lock-free pan gains system
    lock_free_pan_cleanup();
    log_info("SYNTH", "Lock-free pan system cleaned up");
  }

  synth_pool_initialized = 0;
  log_info("SYNTH", "Thread pool shutdown complete");
EOF

echo "${GREEN}✓ Shutdown cleanup patch created${NC}"

echo ""
echo "${YELLOW}Step 3/4: Instructions for synth_additive.c modifications...${NC}"

cat > /tmp/synth_additive_instructions.txt << 'EOF'
MANUAL MODIFICATION REQUIRED for src/synthesis/additive/synth_additive.c

In the synth_IfftMode() function, locate the worker dispatch section and replace:

OLD CODE (around line ~800-850):
  // Dispatch work to threads
  for (int i = 0; i < num_workers; i++) {
    pthread_mutex_lock(&thread_pool[i].work_mutex);
    thread_pool[i].work_ready = 1;
    thread_pool[i].work_done = 0;
    pthread_cond_signal(&thread_pool[i].work_cond);
    pthread_mutex_unlock(&thread_pool[i].work_mutex);
  }

  // Wait for completion
  for (int i = 0; i < num_workers; i++) {
    pthread_mutex_lock(&thread_pool[i].work_mutex);
    while (!thread_pool[i].work_done) {
      struct timespec timeout;
      clock_gettime(CLOCK_REALTIME, &timeout);
      timeout.tv_nsec += 1000000;
      if (timeout.tv_nsec >= 1000000000) {
        timeout.tv_sec += 1;
        timeout.tv_nsec -= 1000000000;
      }
      pthread_cond_timedwait(&thread_pool[i].work_cond, &thread_pool[i].work_mutex, &timeout);
    }
    pthread_mutex_unlock(&thread_pool[i].work_mutex);
  }

NEW CODE:
  // ✅ PHASE 2: Deterministic worker dispatch with barriers
  if (g_use_barriers) {
    // Signal start to all workers (deterministic)
    synth_barrier_wait(&g_worker_start_barrier);
    
    // Wait for all workers to complete (deterministic)
    synth_barrier_wait(&g_worker_end_barrier);
  } else {
    // Fallback to condition variables
    for (int i = 0; i < num_workers; i++) {
      pthread_mutex_lock(&thread_pool[i].work_mutex);
      thread_pool[i].work_ready = 1;
      thread_pool[i].work_done = 0;
      pthread_cond_signal(&thread_pool[i].work_cond);
      pthread_mutex_unlock(&thread_pool[i].work_mutex);
    }

    for (int i = 0; i < num_workers; i++) {
      pthread_mutex_lock(&thread_pool[i].work_mutex);
      while (!thread_pool[i].work_done) {
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_nsec += 1000000;
        if (timeout.tv_nsec >= 1000000000) {
          timeout.tv_sec += 1;
          timeout.tv_nsec -= 1000000000;
        }
        pthread_cond_timedwait(&thread_pool[i].work_cond, &thread_pool[i].work_mutex, &timeout);
      }
      pthread_mutex_unlock(&thread_pool[i].work_mutex);
    }
  }
EOF

cat /tmp/synth_additive_instructions.txt
echo ""
echo "${GREEN}✓ Instructions created${NC}"

echo ""
echo "${YELLOW}Step 4/4: Summary${NC}"
echo ""
echo "📋 Patches created in /tmp/:"
echo "   - worker_patch.txt (for synth_persistent_worker_thread)"
echo "   - shutdown_patch.txt (for synth_shutdown_thread_pool)"
echo "   - synth_additive_instructions.txt (manual modification guide)"
echo ""
echo "⚠️  IMPORTANT: You need to manually apply these patches to:"
echo "   1. src/synthesis/additive/synth_additive_threading.c"
echo "   2. src/synthesis/additive/synth_additive.c"
echo ""
echo "📖 See docs/RT_DETERMINISTIC_THREADING_ACTIVATION.md for detailed instructions"
echo ""
echo "${GREEN}✅ Patch files generated successfully!${NC}"
echo ""
echo "Next steps:"
echo "1. Review the patches in /tmp/"
echo "2. Apply them manually to the source files"
echo "3. Compile with: make clean && make"
echo "4. Test the deterministic mode"
echo ""
echo "To disable barriers if needed, set g_use_barriers = 0 in the code"
