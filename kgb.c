/*
File Name: kgd.c
Descroption: The header file for KGB(Kernel Geography Block). 
    KGB is a memory page to present the target landmarks to
    the host side of Nano Debugger.
Author: GEDU Shanghai Lab(GSL)     
*/
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/kallsyms.h>
#include <linux/vmalloc.h> // for vmalloc
#include "kgb.h"
#include "mailbox.h"
#include "dbgrdata.h"

/////////////////////////////////////////////////////////////////
extern  ndb_mailbox_t* g_ptr_mailbox;
extern struct cpuinfo_x86	boot_cpu_data;
extern struct task_struct   init_task;
struct module         module_kernel, module_nt;
struct list_head        g_module_list_head,*g_module_head_orig=NULL;
const uint32_t NDP_MARKER_A = 0x20180821;
const uint32_t NDP_MARKER_B = 0x47454455; //GEDU
typedef char* (*log_buf_addr_get_t)(void);
typedef u32(*log_buf_len_get_t)(void);
struct list_head g_dbgrdatalist = { NULL, NULL };
dbgrdata64_t  g_dbgrdataentry; // entry for Nano Debugger

typedef struct _kgb_t
{
    ndb_syspara_t*   syspara_;
    struct page*     pages_;
    char*            msg_; // message buffer, allocated on demand
}kgb_t;

kgb_t g_kgb={0};

/////////////////////////////////////////////////////////////////

static int kgb_init_dbgrdata(void)
{
    INIT_LIST_HEAD(&g_dbgrdatalist);
    INIT_LIST_HEAD(&g_dbgrdataentry.head.list);
    g_dbgrdataentry.Revision = 1;

    list_add_tail(&g_dbgrdataentry.head.list, &g_dbgrdatalist);

    return 0;
}

static void* kgb_orig_modules_head(long module_addr)
{
    struct module* mod;
    struct  list_head* next, * list_head_orig;

    mod = __module_address(module_addr);
    list_head_orig = mod->list.prev;
    next = list_head_orig->next;

    printk("module list head (original) = %px\n", list_head_orig);

    return (void*)list_head_orig;
}

static unsigned long kgb_kernel_base(void)
{
    return kallsyms_lookup_name("_text");
}
/*
#ifdef CONFIG_X86_64
    return 0x20000000; // 512MB
#else
    if (func_sym_tab[VAR_ETEXT].func_addr != 0)
        return (long)func_sym_tab[VAR_ETEXT].func_addr - ndb_kernel_base();
    return 0x1200000; // estimation, TODO for detection
#endif
ffffffffb4200000 T _text
ffffffffb5000e91 T _etext
ffffffffb6200000 B __bss_stop
*/
static unsigned long kgb_kernel_length(unsigned long base)
{
    long end = kallsyms_lookup_name("__bss_stop");
    return end - base;
}

static long kgb_module_head(long* head_orig)
{
    struct  list_head * head;

    module_kernel.state = 3;
    module_kernel.core_layout.base = (void*)kgb_kernel_base();
    module_kernel.core_layout.size = kgb_kernel_length(kgb_kernel_base());
    module_nt.state = 3;
    module_nt.core_layout.base = (void*)0xffeeffee80000000;
    module_nt.core_layout.size = 0xa00000;
    strlcpy(module_kernel.name, "vmlinux", 8);
    strlcpy(module_nt.name, "nt", 3);

    INIT_LIST_HEAD(&module_kernel.list);
    INIT_LIST_HEAD(&module_nt.list);
    INIT_LIST_HEAD(&g_module_list_head);
    head = (struct  list_head*)head_orig;
    printk("head = %px\n", head);
    list_add_tail(&module_nt.list, &g_module_list_head);
    list_add_tail(&module_kernel.list, &g_module_list_head);
    module_kernel.list.next = head->next;

    return (long)&g_module_list_head;
}
static int kgb_init_syspara(ptr_ndb_syspara_t psyspara, long module_addr)
{
    long total_cpu = 0;
    log_buf_addr_get_t log_buf_get;
    log_buf_len_get_t  log_len_get;
    ndb_os_struct_sym* sym = &psyspara->kernelstructs_;

    g_module_head_orig = (struct list_head*)kgb_orig_modules_head(module_addr);
    log_buf_get = (log_buf_addr_get_t)kallsyms_lookup_name("log_buf_addr_get");
    log_len_get = (log_buf_len_get_t)kallsyms_lookup_name("log_buf_len_get");
    for_each_online_cpu(total_cpu);
    total_cpu = num_online_cpus();
    psyspara->ndp_markera_ = NDP_MARKER_A;
    psyspara->ndp_markerb_ = NDP_MARKER_B;
    psyspara->kernel_base_ = kgb_kernel_base();
    psyspara->kernel_length_ = kgb_kernel_length(psyspara->kernel_base_);
    psyspara->kgb_base_ = (uint64_t)psyspara;
    psyspara->module_list_head_orig_ = (uint64_t)g_module_head_orig;
    psyspara->module_list_head_ = kgb_module_head((long*)g_module_head_orig);
    psyspara->machine_type_ = boot_cpu_data.x86;
    psyspara->process_list_head_ = (uint64_t)&init_task.tasks;
    psyspara->total_cpu_ = total_cpu;
    psyspara->printk_buffer_addr_ = (uint64_t)log_buf_get();
    psyspara->printk_buffer_length_ = (uint32_t)log_len_get();
    psyspara->percpu_offset_ = (uint64_t)__per_cpu_offset;
    psyspara->debugger_data_list_head_ = (uint64_t)&g_dbgrdatalist;
    psyspara->mailbox_base_ = (uint64_t)g_ptr_mailbox;
    psyspara->phys_base_ = kallsyms_lookup_name("phys_base");
    sym->vma_size_ = sizeof(struct vm_area_struct);
    sym->vma_file_ = offsetof(struct vm_area_struct, vm_file);
    sym->module_size_ = sizeof(struct module);
    sym->module_name_ = offsetof(struct module, name);
    sym->module_version_ = offsetof(struct module, version);
    sym->module_srcversion_ = offsetof(struct module, srcversion);
    sym->module_syms_ = offsetof(struct module, syms);
    sym->module_crcs_ = offsetof(struct module, crcs);
    sym->module_num_syms_ = offsetof(struct module, num_syms);
    sym->module_init_ = offsetof(struct module, init);
    sym->module_list_ = offsetof(struct module, list);
    sym->module_args_ = offsetof(struct module, args);
    sym->module_exit_ = offsetof(struct module, exit);
    sym->module_core_ = offsetof(struct module, core_layout);
    sym->module_core_size_ = offsetof(struct module, core_layout) + offsetof(struct module_layout, size);
    sym->module_core_text_size_ = offsetof(struct module, core_layout) + offsetof(struct module_layout, text_size);
    sym->module_init_size_ = offsetof(struct module, init_layout) + offsetof(struct module_layout, size);
    sym->module_init_text_size_ = offsetof(struct module, init_layout) + offsetof(struct module_layout, text_size);
    sym->module_sect_attrs_ = offsetof(struct module, sect_attrs);
    sym->module_attribute_size_ = sizeof(struct module_attribute);
    sym->attribute_group_size_ = sizeof(struct attribute_group);
    sym->pcpu_current_task_ = (uint32_t)kallsyms_lookup_name("current_task");

    sym->task_size = sizeof(struct task_struct);
    sym->task_state = offsetof(struct task_struct, state);
    sym->task_stack = offsetof(struct task_struct, stack);
    sym->task_usage = offsetof(struct task_struct, usage);
    sym->task_flags = offsetof(struct task_struct, flags);
    sym->task_tasks = offsetof(struct task_struct, tasks);
    sym->task_pid  = offsetof(struct task_struct, pid);
    sym->task_comm = offsetof(struct task_struct, comm);
    sym->task_mm = offsetof(struct task_struct, mm);
    sym->mm_size = sizeof(struct mm_struct);
    sym->task_real_parent = offsetof(struct task_struct, real_parent);
    sym->task_start_time = offsetof(struct task_struct, start_time);
    sym->task_fs = offsetof(struct task_struct, fs);
    sym->task_files = offsetof(struct task_struct, files);
    sym->task_thread = offsetof(struct task_struct, thread);
    sym->mm_pgd = offsetof(struct mm_struct, pgd);

    printk("NDB: syspara is inited. Kernel base:%px length:%d module head:%px task head:%px\n",
        (void*)psyspara->kernel_base_, psyspara->kernel_length_, 
        (void*)g_module_head_orig, (void*)psyspara->process_list_head_);

    return 0;
}
static void* kgb_map(long pfn)
{
    void __iomem* base;
    long paddr = pfn << 12;

    set_fixmap_io(FIX_DBGP_BASE, paddr & PAGE_MASK);
    base = (void __iomem*)__fix_to_virt(FIX_DBGP_BASE);
    base += paddr & ~PAGE_MASK;

    return base;
}

static int kgb_plant(void)
{
    long    pfn = 0;

    g_kgb.pages_ = alloc_pages_node(numa_node_id(), __GFP_HIGHMEM | __GFP_ZERO, 0);
    if (!g_kgb.pages_)
    {
        printk("alloc page for KGB failed\n");
        return -ENOMEM;
    }
    pfn = page_to_pfn(g_kgb.pages_);
    g_kgb.syspara_ = kgb_map(pfn);
    if (!g_kgb.syspara_)
    {
        printk("ndb_map_memory is error\n");
        return -1;
    }
    printk("kgb is mappped at %px (pfn=%lx)\n", g_kgb.syspara_, pfn);

    return 0;
}

void kgb_list_process(void)
{
    struct list_head* ptr_list_next;
    struct task_struct* temp_task;

    ptr_list_next = init_task.tasks.next;
    while (ptr_list_next != &init_task.tasks)
    {
        temp_task = list_entry(ptr_list_next, struct task_struct, tasks);
        printk("pid = %d, process name = %s\n", temp_task->pid, temp_task->comm);
        ptr_list_next = ptr_list_next->next;
    }
}

void kgb_list_module(void)
{
    struct  module* mod;
    struct  list_head* next;
    struct  list_head* head = (struct  list_head*)g_kgb.syspara_->module_list_head_;
    struct list_head* orig_head = (struct  list_head*)g_kgb.syspara_->module_list_head_orig_;

    next = head->next;
    while (next != orig_head)
    {
        mod = list_entry(next, struct module, list);
        printk("module %s @ %px\n", mod->name, next);
        next = next->next;
    }
}
#define KGB_MSG_MAX 500
const char* kgb_message(void)
{
    ptr_ndb_syspara_t     syspara;
    ptr_ndb_os_struct_sym       ptr_sym;
    char * lpsz;

    if(g_kgb.msg_==NULL)
        g_kgb.msg_ = vmalloc(KGB_MSG_MAX);

    syspara = (ptr_ndb_syspara_t)g_kgb.syspara_;
    ptr_sym = &syspara->kernelstructs_;

    lpsz = g_kgb.msg_;
    sprintf(lpsz, "marker_=%x,%x kgb=%px kernel=%px\n", syspara->ndp_markera_, syspara->ndp_markerb_,
        (void*)syspara->kgb_base_,(void*)syspara->kernel_base_);
    lpsz = lpsz + strlen(lpsz);
    sprintf(lpsz, "module list=%px orig %px,process list_=%px\n", (void*)syspara->module_list_head_,
        (void*)syspara->module_list_head_orig_,(void*)syspara->process_list_head_);
    lpsz = lpsz + strlen(lpsz);
    sprintf(lpsz, "vma_size_=%d,vma_file_=%d\n", ptr_sym->vma_size_, ptr_sym->vma_file_);
    lpsz = lpsz + strlen(lpsz);
    sprintf(lpsz, "srcversion_=%d, syms_=%d\n", ptr_sym->module_srcversion_, ptr_sym->module_syms_);
    lpsz = lpsz + strlen(lpsz);
    sprintf(lpsz, "crcs_=%d,num_syms_=%d\n", ptr_sym->module_crcs_, ptr_sym->module_num_syms_);
    lpsz = lpsz + strlen(lpsz);
    sprintf(lpsz, "module_init_=%d, module_list_=%d\n", ptr_sym->module_init_, ptr_sym->module_list_);
    lpsz = lpsz + strlen(lpsz);
    sprintf(lpsz, "args_=%d, exit_=%d\n", ptr_sym->module_args_, ptr_sym->module_exit_);
    lpsz = lpsz + strlen(lpsz);
    sprintf(lpsz, "module_core_=%d, core_size_=%d\n", ptr_sym->module_core_, ptr_sym->module_core_size_);
    lpsz = lpsz + strlen(lpsz);
    sprintf(lpsz, "core_text_size_=%d, init_size_=%d\n", ptr_sym->module_core_text_size_, ptr_sym->module_init_size_);
    lpsz = lpsz + strlen(lpsz);
    sprintf(lpsz, "printk_buffer_addr_=%llx, printk_buffer_length_=%d\n", 
        syspara->printk_buffer_addr_, syspara->printk_buffer_length_);
    lpsz = lpsz + strlen(lpsz);
    sprintf(lpsz, "total_cpu_=%d, machine_type_=%d\n", syspara->total_cpu_, syspara->machine_type_);

    return g_kgb.msg_;
}
static int kgb_on_module_load(struct notifier_block *self, unsigned long val, void * data)
{
    //
    //update the fake module head
    module_kernel.list.next = g_module_head_orig->next;
    //
    printk("NGB module head is updated %p on self %p val %lx, data %p\n", 
		g_module_head_orig, self, val, data);

    return 0;
}
static struct notifier_block kgb_module_load_nb = {
    .notifier_call = kgb_on_module_load,
    };

int kgb_init(void)
{
    int ret = 0;

    ret = kgb_plant();
    if(ret !=0 )
    {
        printk("Plant of KGB failed with %x\n", ret);
        return ret;
    }
    kgb_init_dbgrdata();
    
    ret = kgb_init_syspara(g_kgb.syspara_, (long)kgb_init);
    
    register_module_notifier(&kgb_module_load_nb);

    return ret;
}
int kgb_exit(void)
{
    unregister_module_notifier(&kgb_module_load_nb);

    if(g_kgb.pages_)
        __free_pages(g_kgb.pages_, 0);

    if(g_kgb.msg_!=NULL)
        vfree(g_kgb.msg_);

    return 0;    
}
