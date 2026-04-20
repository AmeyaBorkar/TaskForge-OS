"""
TaskForge v2 -- Banking System on a Custom OS Kernel
Flask backend: OS kernel (processes, scheduler, memory, cache, filesystem,
deadlock detection via Banker's algorithm, disk I/O) plus a banking
application that exercises every subsystem through syscalls.
"""
import time, random
from datetime import datetime
from flask import Flask, jsonify, request, render_template

# ---------------------------------------------------------------------------
#  OS Kernel
# ---------------------------------------------------------------------------
class Kernel:
    VALID_TRANSITIONS = {
        'NEW': ['READY'], 'READY': ['RUNNING'],
        'RUNNING': ['READY', 'WAITING', 'TERMINATED'],
        'WAITING': ['READY'], 'TERMINATED': []
    }

    def __init__(self):
        self.processes, self.next_pid, self.clock_tick, self.rr_index = [], 1, 0, 0
        self.sched_algo, self.quantum = 'FCFS', 5
        # Memory
        self.mem_total = 4096
        self.mem_blocks = [{'start': 0, 'size': 4096, 'free': True, 'owner': -1}]
        self.mem_strategy, self.last_alloc_idx = 'FIRST_FIT', 0
        # Cache (16 slots)
        self.cache = [{'page_id': -1, 'ref_bit': 0, 'last_access': 0, 'load_time': 0} for _ in range(16)]
        self.cache_algo = 'LRU'
        self.cache_hits = self.cache_misses = self.cache_tick = self.clock_hand = 0
        # Filesystem
        self.fs_nodes = [{'id': 0, 'name': '/', 'is_dir': True, 'parent': -1,
                          'data': '', 'size': 0, 'created': time.time(), 'active': True}]
        self.next_fid = 1
        # Deadlock / Banker's
        self.resources = {i: 10 for i in range(10)}
        self.allocation, self.max_need = {}, {}
        self.deadlock_count, self.prevention_on = 0, True
        # Disk I/O
        self.disk_algo, self.disk_head, self.disk_dir = 'FCFS', 100, 1
        self.disk_queue = []
        self.total_seek = self.total_ops = self.buf_reads = self.buf_writes = 0
        # Trace
        self.trace, self.boot_time = [], time.time()

    # -- helpers -----------------------------------------------------------
    def _ts(self):
        return datetime.now().strftime('[%H:%M:%S]')

    def trace_log(self, msg):
        self.trace.append(f"{self._ts()} {msg}")
        if len(self.trace) > 200:
            self.trace = self.trace[-200:]

    def _pcb(self, pid):
        return next((p for p in self.processes if p['pid'] == pid), None)

    # -- processes ---------------------------------------------------------
    def create_process(self, name, priority, burst):
        pid = self.next_pid; self.next_pid += 1
        self.processes.append({
            'pid': pid, 'name': name, 'state': 'NEW', 'priority': priority,
            'burst_time': burst, 'remaining_time': burst,
            'arrival_time': self.clock_tick, 'start_time': -1,
            'completion_time': -1, 'turnaround': 0, 'wait_time': 0,
            'mem_allocated': 0, 'active': True
        })
        self.clock_tick += 1
        self.trace_log(f"PROC  Created PID={pid} name={name} prio={priority} burst={burst}")
        return pid

    def set_state(self, pid, new_state):
        pcb = self._pcb(pid)
        if not pcb or new_state not in self.VALID_TRANSITIONS.get(pcb['state'], []):
            return False
        old = pcb['state']; pcb['state'] = new_state
        if new_state == 'RUNNING' and pcb['start_time'] == -1:
            pcb['start_time'] = self.clock_tick
        if new_state == 'TERMINATED':
            pcb['completion_time'] = self.clock_tick
            pcb['turnaround'] = pcb['completion_time'] - pcb['arrival_time']
            pcb['wait_time'] = pcb['turnaround'] - pcb['burst_time']
            pcb['active'] = False
        self.clock_tick += 1
        self.trace_log(f"PROC  PID={pid} {old} -> {new_state}")
        return True

    def schedule_next(self):
        ready = [p for p in self.processes if p['state'] == 'READY']
        if not ready:
            return -1
        keys = {'FCFS': lambda p: p['arrival_time'], 'SJF': lambda p: p['remaining_time'],
                'PRIORITY': lambda p: p['priority']}
        if self.sched_algo in keys:
            chosen = min(ready, key=keys[self.sched_algo])
        elif self.sched_algo == 'ROUND_ROBIN':
            ready.sort(key=lambda p: p['arrival_time'])
            chosen = ready[self.rr_index % len(ready)]; self.rr_index += 1
        else:
            chosen = ready[0]
        self.set_state(chosen['pid'], 'RUNNING')
        self.trace_log(f"SCHED [{self.sched_algo}] Dispatched PID={chosen['pid']}")
        return chosen['pid']

    def kill_process(self, pid):
        pcb = self._pcb(pid)
        if not pcb:
            return False
        if pcb['state'] != 'TERMINATED':
            pcb.update(state='TERMINATED', completion_time=self.clock_tick, active=False)
            pcb['turnaround'] = pcb['completion_time'] - pcb['arrival_time']
            pcb['wait_time'] = max(0, pcb['turnaround'] - pcb['burst_time'])
            self.clock_tick += 1
        self.mem_free_all(pid); self.resource_release_all(pid)
        self.trace_log(f"PROC  Killed PID={pid}")
        return True

    # -- memory ------------------------------------------------------------
    def mem_alloc(self, pid, size_kb):
        cands = [(i, b) for i, b in enumerate(self.mem_blocks) if b['free'] and b['size'] >= size_kb]
        if not cands:
            self.trace_log(f"MEM   Alloc FAILED PID={pid} size={size_kb} (no space)"); return -1
        if self.mem_strategy == 'FIRST_FIT':   idx, blk = cands[0]
        elif self.mem_strategy == 'BEST_FIT':  idx, blk = min(cands, key=lambda x: x[1]['size'])
        elif self.mem_strategy == 'WORST_FIT': idx, blk = max(cands, key=lambda x: x[1]['size'])
        elif self.mem_strategy == 'NEXT_FIT':
            found = next(((i, b) for i, b in cands if i >= self.last_alloc_idx), cands[0])
            idx, blk = found
        else: idx, blk = cands[0]
        if blk['size'] > size_kb:
            self.mem_blocks.insert(idx + 1, {'start': blk['start'] + size_kb,
                                              'size': blk['size'] - size_kb, 'free': True, 'owner': -1})
        blk['size'], blk['free'], blk['owner'] = size_kb, False, pid
        self.last_alloc_idx = idx
        pcb = self._pcb(pid)
        if pcb: pcb['mem_allocated'] += size_kb
        self.trace_log(f"MEM   Alloc PID={pid} size={size_kb} addr={blk['start']} [{self.mem_strategy}]")
        return blk['start']

    def _coalesce(self):
        i = 0
        while i < len(self.mem_blocks) - 1:
            a, b = self.mem_blocks[i], self.mem_blocks[i + 1]
            if a['free'] and b['free']:
                a['size'] += b['size']; self.mem_blocks.pop(i + 1)
            else: i += 1

    def mem_free(self, pid, addr):
        for blk in self.mem_blocks:
            if blk['start'] == addr and blk['owner'] == pid:
                blk['free'], blk['owner'] = True, -1; self._coalesce()
                self.trace_log(f"MEM   Free PID={pid} addr={addr}"); return True
        return False

    def mem_free_all(self, pid):
        freed = 0
        for blk in self.mem_blocks:
            if blk['owner'] == pid:
                blk['free'], blk['owner'] = True, -1; freed += blk['size']
        if freed:
            self._coalesce(); self.trace_log(f"MEM   FreeAll PID={pid} freed={freed}KB")

    def mem_stats(self):
        used = sum(b['size'] for b in self.mem_blocks if not b['free'])
        free = self.mem_total - used
        free_blks = [b for b in self.mem_blocks if b['free']]
        alloc_blks = [b for b in self.mem_blocks if not b['free']]
        largest = max((b['size'] for b in free_blks), default=0)
        frag = round((1.0 - largest / free) * 100, 2) if free > 0 else 0.0
        return {'total': self.mem_total, 'used': used, 'free': free,
                'strategy': self.mem_strategy,
                'allocated': len(alloc_blks), 'free_blocks': len(free_blks),
                'blocks': len(self.mem_blocks),
                'fragmentation': frag}

    # -- cache -------------------------------------------------------------
    def cache_access(self, page_id):
        self.cache_tick += 1
        for slot in self.cache:
            if slot['page_id'] == page_id:
                slot['last_access'], slot['ref_bit'] = self.cache_tick, 1
                self.cache_hits += 1
                self.trace_log(f"CACHE HIT  page={page_id}"); return True
        self.cache_misses += 1
        victim = self._cache_victim()
        old_page = victim['page_id']
        victim.update(page_id=page_id, ref_bit=1, last_access=self.cache_tick, load_time=self.cache_tick)
        evict = f" evict={old_page}" if old_page != -1 else ""
        self.trace_log(f"CACHE MISS page={page_id}{evict} [{self.cache_algo}]"); return False

    def _cache_victim(self):
        empty = next((s for s in self.cache if s['page_id'] == -1), None)
        if empty: return empty
        if self.cache_algo == 'LRU':   return min(self.cache, key=lambda s: s['last_access'])
        if self.cache_algo == 'FIFO':  return min(self.cache, key=lambda s: s['load_time'])
        # CLOCK
        while True:
            slot = self.cache[self.clock_hand]
            self.clock_hand = (self.clock_hand + 1) % len(self.cache)
            if slot['ref_bit'] == 0: return slot
            slot['ref_bit'] = 0

    def cache_stats(self):
        used = sum(1 for s in self.cache if s['page_id'] != -1)
        total_acc = self.cache_hits + self.cache_misses
        return {'total': len(self.cache), 'used': used,
                'hits': self.cache_hits, 'misses': self.cache_misses,
                'hit_ratio': round((self.cache_hits / total_acc) * 100, 2) if total_acc else 0.0,
                'algorithm': self.cache_algo}

    # -- filesystem --------------------------------------------------------
    def fs_create(self, name, is_dir, parent):
        fid = self.next_fid; self.next_fid += 1
        self.fs_nodes.append({'id': fid, 'name': name, 'is_dir': is_dir, 'parent': parent,
                              'data': '', 'size': 0, 'created': time.time(), 'active': True})
        self.trace_log(f"FS    Create {'DIR' if is_dir else 'FILE'} id={fid} name={name} parent={parent}")
        return fid

    def fs_find(self, name, parent):
        return next((n['id'] for n in self.fs_nodes
                     if n['name'] == name and n['parent'] == parent and n['active']), -1)

    def _fs_node(self, fid):
        return next((n for n in self.fs_nodes if n['id'] == fid and n['active']), None)

    def fs_open(self, file_id, mode, pid):
        self.trace_log(f"FS    Open fid={file_id} mode={mode} PID={pid}"); return file_id

    def fs_read(self, file_id):
        node = self._fs_node(file_id)
        if not node: return ''
        self.cache_access(file_id); self.buf_reads += 1
        self.trace_log(f"FS    Read fid={file_id} size={node['size']}"); return node['data']

    def fs_write(self, file_id, data):
        node = self._fs_node(file_id)
        if not node: return False
        node['data'], node['size'] = data, len(data); self.buf_writes += 1
        self.trace_log(f"FS    Write fid={file_id} size={node['size']}"); return True

    def fs_list(self, dir_id):
        return [n for n in self.fs_nodes if n['parent'] == dir_id and n['active']]

    # -- deadlock / Banker's algorithm -------------------------------------
    def resource_request(self, pid, res_id, count):
        if res_id not in self.resources: return False
        alloc_so_far = self.allocation.get(pid, {}).get(res_id, 0)
        need_left = self.max_need.get(pid, {}).get(res_id, count)
        remaining_need = need_left - alloc_so_far
        if count > remaining_need and pid in self.max_need:
            self.trace_log(f"BANK  DENY PID={pid} res={res_id} count={count} > need={remaining_need}")
            return False
        if count > self.resources[res_id]:
            self.trace_log(f"BANK  DENY PID={pid} res={res_id} count={count} > avail={self.resources[res_id]}")
            return False
        # tentative allocation
        self.resources[res_id] -= count
        self.allocation.setdefault(pid, {})[res_id] = alloc_so_far + count
        if self.prevention_on and not self.banker_safe():
            self.resources[res_id] += count
            self.allocation[pid][res_id] -= count; self.deadlock_count += 1
            self.trace_log(f"BANK  UNSAFE PID={pid} res={res_id} -- rolled back (deadlock prevention)")
            return False
        self.trace_log(f"BANK  GRANT PID={pid} res={res_id} count={count} avail={self.resources[res_id]}")
        return True

    def resource_release(self, pid, res_id, count):
        cur = self.allocation.get(pid, {}).get(res_id, 0)
        release = min(count, cur)
        if release <= 0: return
        self.allocation[pid][res_id] -= release; self.resources[res_id] += release
        self.trace_log(f"BANK  RELEASE PID={pid} res={res_id} count={release} avail={self.resources[res_id]}")

    def resource_release_all(self, pid):
        for res_id, cnt in self.allocation.pop(pid, {}).items():
            if cnt > 0: self.resources[res_id] += cnt
        self.max_need.pop(pid, None)

    def _safety_loop(self):
        """Shared logic for banker_safe / deadlock_check."""
        work = dict(self.resources)
        active_pids = [p['pid'] for p in self.processes if p['active']]
        finish = {pid: False for pid in active_pids}
        changed = True
        while changed:
            changed = False
            for pid in active_pids:
                if finish[pid]: continue
                alloc = self.allocation.get(pid, {})
                need = self.max_need.get(pid, {})
                if all(need.get(r, 0) - alloc.get(r, 0) <= work.get(r, 0) for r in need):
                    finish[pid] = True
                    for r, c in alloc.items(): work[r] = work.get(r, 0) + c
                    changed = True
        return finish

    def banker_safe(self):
        return all(self._safety_loop().values())

    def deadlock_check(self):
        return [pid for pid, done in self._safety_loop().items() if not done]

    # -- disk I/O ----------------------------------------------------------
    def io_request(self, track):
        self.disk_queue.append(track)
        self.trace_log(f"IO    Request track={track} queue_len={len(self.disk_queue)}")

    def io_process(self):
        if not self.disk_queue: return 0
        if self.disk_algo == 'FCFS':
            target = self.disk_queue.pop(0)
        elif self.disk_algo == 'SSTF':
            target = min(self.disk_queue, key=lambda t: abs(t - self.disk_head))
            self.disk_queue.remove(target)
        elif self.disk_algo == 'SCAN':
            in_dir = sorted([t for t in self.disk_queue if (t - self.disk_head) * self.disk_dir >= 0],
                            key=lambda t: abs(t - self.disk_head))
            if in_dir: target = in_dir[0]
            else: self.disk_dir *= -1; target = min(self.disk_queue, key=lambda t: abs(t - self.disk_head))
            self.disk_queue.remove(target)
        elif self.disk_algo == 'C-SCAN':
            ahead = sorted(t for t in self.disk_queue if t >= self.disk_head)
            if ahead: target = ahead[0]
            else: self.disk_head = 0; target = min(self.disk_queue)
            self.disk_queue.remove(target)
        else:
            target = self.disk_queue.pop(0)
        seek = abs(target - self.disk_head)
        self.disk_head = target; self.total_seek += seek; self.total_ops += 1
        self.trace_log(f"IO    Process track={target} seek={seek} [{self.disk_algo}]")
        return seek

    # -- dashboard ---------------------------------------------------------
    def dashboard(self):
        mem, cache = self.mem_stats(), self.cache_stats()
        active = [p for p in self.processes if p['active']]
        done = [p for p in self.processes if not p['active']]
        avg_t = (sum(p['turnaround'] for p in done) / len(done)) if done else 0
        avg_w = (sum(p['wait_time'] for p in done) / len(done)) if done else 0
        fs_files = sum(1 for n in self.fs_nodes if not n['is_dir'] and n['active'])
        fs_dirs = sum(1 for n in self.fs_nodes if n['is_dir'] and n['active'])
        fs_size = sum(n['size'] for n in self.fs_nodes if n['active'])
        detected_pids = self.deadlock_check()
        avail_str = ', '.join(f"R{r}:{c}" for r, c in sorted(self.resources.items()))
        return {
            'uptime': round(time.time() - self.boot_time, 1),
            'clock_tick': self.clock_tick,
            'processes': {'total': len(self.processes), 'active': len(active),
                          'terminated': len(done), 'avg_turnaround': round(avg_t, 2),
                          'avg_wait': round(avg_w, 2),
                          'scheduler': self.sched_algo},
            'scheduler': self.sched_algo,
            'memory': mem, 'cache': cache,
            'filesystem': {'nodes': len(self.fs_nodes), 'files': fs_files,
                           'directories': fs_dirs, 'total_size': f"{fs_size} B"},
            'deadlock': {'prevention': self.prevention_on,
                         'detected': len(detected_pids),
                         'denied': self.deadlock_count,
                         'resources': avail_str},
            'io': {'algorithm': self.disk_algo, 'head_position': self.disk_head,
                   'queue_len': len(self.disk_queue),
                   'total_seek': self.total_seek,
                   'operations': self.total_ops,
                   'avg_seek': round(self.total_seek / self.total_ops, 2) if self.total_ops else 0,
                   'reads': self.buf_reads, 'writes': self.buf_writes}
        }

# ---------------------------------------------------------------------------
#  Banking Application
# ---------------------------------------------------------------------------
class BankApp:
    def __init__(self, kernel):
        self.kernel = kernel
        self.accounts, self.transactions = [], []
        self.next_acc_id = self.next_tx_id = 1
        self.accounts_dir = kernel.fs_create('accounts', True, 0)
        self.logs_dir = kernel.fs_create('logs', True, 0)

    def _acc(self, acc_id):
        return next((a for a in self.accounts if a['id'] == acc_id and a['active']), None)

    def _record_tx(self, tx_type, from_acc, to_acc, amount, pid):
        tx = {'id': self.next_tx_id, 'type': tx_type, 'from_acc': from_acc, 'to_acc': to_acc,
              'amount': amount, 'pid': pid, 'timestamp': datetime.now().strftime('%Y-%m-%d %H:%M:%S')}
        self.next_tx_id += 1; self.transactions.append(tx)

    def _boot_proc(self, name, prio, burst, mem_kb=1):
        """Create process, transition NEW->READY->RUNNING, allocate memory."""
        k = self.kernel
        pid = k.create_process(name, prio, burst)
        k.set_state(pid, 'READY'); k.schedule_next()
        k.mem_alloc(pid, mem_kb)
        return pid

    def _teardown(self, pid):
        """Free all memory and terminate the process."""
        k = self.kernel
        k.mem_free_all(pid); k.set_state(pid, 'TERMINATED')

    def _do_io(self):
        self.kernel.io_request(random.randint(0, 200)); self.kernel.io_process()

    # -- create account ----------------------------------------------------
    def create_account(self, name, initial_balance):
        k = self.kernel; ts = len(k.trace)
        pid = self._boot_proc('create_account', 3, 10)
        acc_id = self.next_acc_id; self.next_acc_id += 1
        fid = k.fs_create(f'acc_{acc_id}.dat', False, self.accounts_dir)
        k.fs_open(fid, 'w', pid)
        k.fs_write(fid, f'{name}|{initial_balance}')
        k.cache_access(acc_id)
        res_id = acc_id % 10
        k.max_need.setdefault(pid, {})[res_id] = 2
        k.allocation.setdefault(pid, {})[res_id] = 0
        self.accounts.append({'id': acc_id, 'holder': name, 'balance': initial_balance,
                              'active': True, 'file_id': fid, 'resource_id': res_id})
        self._record_tx('CREATE', acc_id, -1, initial_balance, pid)
        self._do_io(); self._teardown(pid)
        return {'success': True, 'account_id': acc_id, 'trace': k.trace[ts:]}

    # -- deposit -----------------------------------------------------------
    def deposit(self, acc_id, amount):
        k = self.kernel; ts = len(k.trace)
        acc = self._acc(acc_id)
        if not acc:
            k.trace_log(f"BANK  Deposit FAILED -- account {acc_id} not found")
            return {'success': False, 'error': 'Account not found', 'trace': k.trace[ts:]}
        if amount <= 0:
            k.trace_log(f"BANK  Deposit FAILED -- invalid amount {amount}")
            return {'success': False, 'error': 'Amount must be positive', 'trace': k.trace[ts:]}
        pid = self._boot_proc('deposit', 2, 8)
        res_id = acc['resource_id']
        k.max_need.setdefault(pid, {})[res_id] = 1
        k.allocation.setdefault(pid, {})[res_id] = 0
        if not k.resource_request(pid, res_id, 1):
            self._teardown(pid)
            return {'success': False, 'error': "Resource lock denied by Banker's algorithm",
                    'trace': k.trace[ts:]}
        k.cache_access(acc_id)
        k.fs_open(acc['file_id'], 'rw', pid); k.fs_read(acc['file_id'])
        acc['balance'] += amount
        k.fs_write(acc['file_id'], f"{acc['holder']}|{acc['balance']}")
        self._do_io(); self._record_tx('DEPOSIT', acc_id, -1, amount, pid)
        k.resource_release(pid, res_id, 1); self._teardown(pid)
        return {'success': True, 'balance': acc['balance'], 'trace': k.trace[ts:]}

    # -- withdraw ----------------------------------------------------------
    def withdraw(self, acc_id, amount):
        k = self.kernel; ts = len(k.trace)
        acc = self._acc(acc_id)
        if not acc:
            k.trace_log(f"BANK  Withdraw FAILED -- account {acc_id} not found")
            return {'success': False, 'error': 'Account not found', 'trace': k.trace[ts:]}
        if amount <= 0:
            k.trace_log(f"BANK  Withdraw FAILED -- invalid amount {amount}")
            return {'success': False, 'error': 'Amount must be positive', 'trace': k.trace[ts:]}
        pid = self._boot_proc('withdraw', 2, 8)
        res_id = acc['resource_id']
        k.max_need.setdefault(pid, {})[res_id] = 1
        k.allocation.setdefault(pid, {})[res_id] = 0
        if not k.resource_request(pid, res_id, 1):
            self._teardown(pid)
            return {'success': False, 'error': "Resource lock denied by Banker's algorithm",
                    'trace': k.trace[ts:]}
        k.cache_access(acc_id)
        k.fs_open(acc['file_id'], 'rw', pid); k.fs_read(acc['file_id'])
        if acc['balance'] < amount:
            k.trace_log(f"BANK  Withdraw DENIED -- insufficient balance (has={acc['balance']}, need={amount})")
            k.resource_release(pid, res_id, 1); self._teardown(pid)
            return {'success': False, 'error': 'Insufficient balance',
                    'balance': acc['balance'], 'trace': k.trace[ts:]}
        acc['balance'] -= amount
        k.fs_write(acc['file_id'], f"{acc['holder']}|{acc['balance']}")
        self._do_io(); self._record_tx('WITHDRAW', acc_id, -1, amount, pid)
        k.resource_release(pid, res_id, 1); self._teardown(pid)
        return {'success': True, 'balance': acc['balance'], 'trace': k.trace[ts:]}

    # -- transfer ----------------------------------------------------------
    def transfer(self, from_id, to_id, amount):
        k = self.kernel; ts = len(k.trace)
        src, dst = self._acc(from_id), self._acc(to_id)
        if not src or not dst:
            k.trace_log("BANK  Transfer FAILED -- account not found")
            return {'success': False, 'error': 'Account not found', 'trace': k.trace[ts:]}
        if amount <= 0:
            k.trace_log(f"BANK  Transfer FAILED -- invalid amount {amount}")
            return {'success': False, 'error': 'Amount must be positive', 'trace': k.trace[ts:]}
        if from_id == to_id:
            k.trace_log("BANK  Transfer FAILED -- same account")
            return {'success': False, 'error': 'Cannot transfer to same account', 'trace': k.trace[ts:]}
        pid = self._boot_proc('transfer', 1, 15, mem_kb=2)
        # resource ordering: lock lower ID first to prevent deadlock
        first, second = (src, dst) if src['resource_id'] <= dst['resource_id'] else (dst, src)
        r1, r2 = first['resource_id'], second['resource_id']
        k.trace_log(f"BANK  Transfer resource ordering: lock res={r1} before res={r2}")
        k.max_need.setdefault(pid, {}).update({r1: 1, r2: 1})
        k.allocation.setdefault(pid, {}).update({r1: 0, r2: 0})
        if not k.resource_request(pid, r1, 1):
            k.trace_log(f"BANK  Transfer DENIED -- Banker's denied lock on res={r1}")
            self._teardown(pid)
            return {'success': False, 'error': f"Banker's algorithm denied resource {r1}",
                    'trace': k.trace[ts:]}
        if not k.resource_request(pid, r2, 1):
            k.trace_log(f"BANK  Transfer DENIED -- Banker's denied lock on res={r2}")
            k.resource_release(pid, r1, 1); self._teardown(pid)
            return {'success': False, 'error': f"Banker's algorithm denied resource {r2}",
                    'trace': k.trace[ts:]}
        k.cache_access(from_id); k.cache_access(to_id)
        k.fs_read(src['file_id']); k.fs_read(dst['file_id'])
        if src['balance'] < amount:
            k.trace_log(f"BANK  Transfer DENIED -- insufficient balance (has={src['balance']}, need={amount})")
            k.resource_release(pid, r2, 1); k.resource_release(pid, r1, 1); self._teardown(pid)
            return {'success': False, 'error': 'Insufficient balance',
                    'balance': src['balance'], 'trace': k.trace[ts:]}
        src['balance'] -= amount; dst['balance'] += amount
        k.fs_write(src['file_id'], f"{src['holder']}|{src['balance']}")
        k.fs_write(dst['file_id'], f"{dst['holder']}|{dst['balance']}")
        self._do_io(); self._record_tx('TRANSFER', from_id, to_id, amount, pid)
        # release in reverse order
        k.resource_release(pid, r2, 1); k.resource_release(pid, r1, 1); self._teardown(pid)
        return {'success': True, 'from_balance': src['balance'],
                'to_balance': dst['balance'], 'trace': k.trace[ts:]}

    # -- check balance -----------------------------------------------------
    def check_balance(self, acc_id):
        k = self.kernel; ts = len(k.trace)
        acc = self._acc(acc_id)
        if not acc:
            k.trace_log(f"BANK  Balance check FAILED -- account {acc_id} not found")
            return {'success': False, 'error': 'Account not found', 'trace': k.trace[ts:]}
        pid = k.create_process('check_balance', 5, 3)
        k.set_state(pid, 'READY'); k.schedule_next()
        cached = k.cache_access(acc_id); k.fs_read(acc['file_id'])
        k.set_state(pid, 'TERMINATED')
        return {'success': True, 'balance': acc['balance'], 'cached': cached, 'trace': k.trace[ts:]}

# ---------------------------------------------------------------------------
#  Flask Application
# ---------------------------------------------------------------------------
app = Flask(__name__)
kernel = Kernel()
bank = BankApp(kernel)

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api/account/create', methods=['POST'])
def api_create_account():
    data = request.json
    return jsonify(bank.create_account(data['name'], float(data['balance'])))

@app.route('/api/account/deposit', methods=['POST'])
def api_deposit():
    data = request.json
    return jsonify(bank.deposit(int(data['account_id']), float(data['amount'])))

@app.route('/api/account/withdraw', methods=['POST'])
def api_withdraw():
    data = request.json
    return jsonify(bank.withdraw(int(data['account_id']), float(data['amount'])))

@app.route('/api/account/transfer', methods=['POST'])
def api_transfer():
    data = request.json
    return jsonify(bank.transfer(int(data['from_id']), int(data['to_id']), float(data['amount'])))

@app.route('/api/account/<int:acc_id>/balance')
def api_balance(acc_id):
    return jsonify(bank.check_balance(acc_id))

@app.route('/api/account/balance', methods=['POST'])
def api_balance_post():
    data = request.json
    return jsonify(bank.check_balance(int(data['account_id'])))

@app.route('/api/accounts')
def api_accounts():
    return jsonify({'accounts': bank.accounts, 'transactions': bank.transactions[-20:]})

@app.route('/api/dashboard')
def api_dashboard():
    return jsonify(kernel.dashboard())

@app.route('/api/config', methods=['POST'])
def api_config():
    data = request.json
    if 'scheduler' in data: kernel.sched_algo = data['scheduler']
    if 'quantum' in data:   kernel.quantum = int(data['quantum'])
    if 'memory' in data:    kernel.mem_strategy = data['memory']
    if 'cache' in data:     kernel.cache_algo = data['cache']
    if 'disk' in data:      kernel.disk_algo = data['disk']
    kernel.trace_log(f"CONFIG Updated: {data}")
    return jsonify({'success': True})

@app.route('/api/trace')
def api_trace():
    return jsonify({'trace': kernel.trace[-50:]})

@app.route('/api/trace/clear', methods=['POST'])
def api_trace_clear():
    kernel.trace.clear()
    return jsonify({'success': True})

@app.route('/api/concepts')
def api_concepts():
    """OS syllabus -> implementation mapping for the course-project showcase."""
    d = kernel.dashboard()
    units = [
        {
            'unit': 'Unit I',
            'title': 'Introduction to OS',
            'owner': 'Ameya Borkar',
            'topics': [
                {'name': 'System Calls',
                 'impl': 'kernel/os_syscall.h + Kernel.* methods in web/app.py',
                 'live': f"clock_tick = {d['clock_tick']} (every syscall advances it)"},
                {'name': 'OS Services (process / memory / file / IO / comm / error-detection)',
                 'impl': 'kernel/os_kernel.c',
                 'live': f"uptime = {d['uptime']}s"},
                {'name': 'OS Types demonstrated',
                 'impl': 'Multiprogramming + Time-sharing (multiple procs, preemptive sched)',
                 'live': f"active processes = {d['processes']['active']}"},
            ],
        },
        {
            'unit': 'Unit II',
            'title': 'Process Management',
            'owner': 'Ameya (PCB, states) + Aditya (sync) + Ayush (concurrency demos)',
            'topics': [
                {'name': 'Process Concept + PCB + 5-state model',
                 'impl': 'Kernel.create_process / set_state (NEW->READY->RUNNING->WAITING->TERMINATED)',
                 'live': f"total procs = {d['processes']['total']}, terminated = {d['processes']['terminated']}, avg turnaround = {d['processes']['avg_turnaround']}"},
                {'name': 'Mutual Exclusion (H/W, S/W, Semaphores, Mutex, Monitors)',
                 'impl': 'src/process_mgmt.c; Kernel.resource_request uses semaphore counts',
                 'live': f"resource vector = {d['deadlock']['resources']}"},
                {'name': 'Classical problems (Readers-Writers, Producer-Consumer, Dining Philosophers)',
                 'impl': 'src/process_mgmt.c (standalone demos in v1 simulator)',
                 'live': 'run ./taskforge.exe for interactive demos'},
            ],
        },
        {
            'unit': 'Unit III',
            'title': 'Process Scheduling',
            'owner': 'Ayush Agnihotri',
            'topics': [
                {'name': 'Scheduling Algorithms: FCFS, SJF, Round Robin, Priority',
                 'impl': 'src/scheduler.c + Kernel.schedule_next',
                 'live': f"current scheduler = {d['scheduler']} (switch in Configuration tab)"},
                {'name': 'Preemptive vs Non-preemptive, Long/Medium/Short-term schedulers',
                 'impl': 'src/scheduler.c',
                 'live': f"avg wait time = {d['processes']['avg_wait']}"},
            ],
        },
        {
            'unit': 'Unit IV',
            'title': 'Deadlocks',
            'owner': 'Aditya Chimurkar',
            'topics': [
                {'name': "Banker's Algorithm (Avoidance) + safety check",
                 'impl': 'Kernel.banker_safe / _safety_loop; every resource_request is validated',
                 'live': f"unsafe requests denied = {d['deadlock']['denied']}"},
                {'name': 'Deadlock Prevention (resource ordering on Transfer)',
                 'impl': 'BankApp.transfer locks lower resource_id first',
                 'live': f"prevention enabled = {d['deadlock']['prevention']}"},
                {'name': 'Deadlock Detection + Recovery',
                 'impl': 'Kernel.deadlock_check (same safety loop, reports stuck PIDs)',
                 'live': f"currently stuck procs = {d['deadlock']['detected']}"},
            ],
        },
        {
            'unit': 'Unit V',
            'title': 'Memory Management',
            'owner': 'Arnav Gupta',
            'topics': [
                {'name': 'Placement: First Fit, Best Fit, Worst Fit, Next Fit + coalescing',
                 'impl': 'src/memory_mgmt.c + Kernel.mem_alloc / _coalesce',
                 'live': f"strategy = {d['memory']['strategy']}, used = {d['memory']['used']}/{d['memory']['total']} KB, fragmentation = {d['memory']['fragmentation']}%"},
                {'name': 'Virtual Memory: Paging, Segmentation, TLB',
                 'impl': 'src/memory_mgmt.c (v1 simulator demos)',
                 'live': f"blocks allocated = {d['memory']['allocated']}, free = {d['memory']['free_blocks']}"},
                {'name': 'Page Replacement: FIFO, LRU, Clock',
                 'impl': 'Kernel.cache_access / _cache_victim (page cache for account records)',
                 'live': f"algo = {d['cache']['algorithm']}, hits = {d['cache']['hits']}, misses = {d['cache']['misses']}, hit ratio = {d['cache']['hit_ratio']}%"},
            ],
        },
        {
            'unit': 'Unit VI',
            'title': 'I/O and File Management',
            'owner': 'Aarush Bakshi',
            'topics': [
                {'name': 'Disk Scheduling: FCFS, SSTF, SCAN, C-SCAN',
                 'impl': 'src/io_file_mgmt.c + Kernel.io_process',
                 'live': f"algo = {d['io']['algorithm']}, head = {d['io']['head_position']}, total seek = {d['io']['total_seek']}, ops = {d['io']['operations']}"},
                {'name': 'I/O Buffering + device characteristics',
                 'impl': 'Kernel.fs_read/fs_write route through cache and buf counters',
                 'live': f"buffered reads = {d['io']['reads']}, writes = {d['io']['writes']}"},
                {'name': 'File Management: directories, file sharing, free space',
                 'impl': 'Kernel.fs_create / fs_open / fs_read / fs_write (VFS)',
                 'live': f"files = {d['filesystem']['files']}, directories = {d['filesystem']['directories']}, total size = {d['filesystem']['total_size']}"},
            ],
        },
    ]
    return jsonify({'units': units,
                    'team': [
                        {'name': 'Ameya Borkar', 'role': 'Lead — Architecture, Syscalls, Process Mgmt, Integration'},
                        {'name': 'Ayush Agnihotri', 'role': 'CPU Scheduling, Concurrency / Producer-Consumer'},
                        {'name': 'Arnav Gupta', 'role': 'Memory Placement, Virtual Memory, Page Replacement'},
                        {'name': 'Aditya Chimurkar', 'role': "Deadlocks (Banker's), Mutual Exclusion, Sync problems"},
                        {'name': 'Aarush Bakshi', 'role': 'Disk Scheduling, I/O Buffering, File Management'},
                    ]})

if __name__ == '__main__':
    print("TaskForge OS Kernel booting...")
    print(f"  Memory   : {kernel.mem_total} KB")
    print(f"  Cache    : {len(kernel.cache)} slots ({kernel.cache_algo})")
    print(f"  Scheduler: {kernel.sched_algo}")
    print(f"  Disk I/O : {kernel.disk_algo}")
    print("Open http://localhost:5000 in your browser")
    app.run(debug=False, port=5000)
