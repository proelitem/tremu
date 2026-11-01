"""Our Turnip for Quest: Horizon OS refuses the old KGSL memory ioctls with EACCES (GPUMEM_ALLOC_ID,
GPUMEM_GET_INFO, GPUMEM_FREE_ID) but allows the GPUOBJ family (GPUOBJ_ALLOC, GPUOBJ_INFO, GPUOBJ_FREE),
as proved by _tmp/turnip/kgslprobe2.c. This patches Mesa's tu_knl_kgsl.cc so the old calls fall back to
the new ones on EACCES; other devices keep the normal path. Usage: run from the Mesa source folder.
Can be run again over an older version of itself."""
import re
from pathlib import Path

FILE = Path('src/freedreno/vulkan/tu_knl_kgsl.cc')
s = FILE.read_text(encoding='utf-8')

BEGIN, END = '/* WFG-QUEST-BEGIN */\n', '/* WFG-QUEST-END */\n'
HELPERS = BEGIN + '''/* Horizon OS (Quest) refuses the GPUMEM_* memory ioctls with EACCES but allows GPUOBJ_*. */
static int
kgsl_get_info_compat(int fd, struct kgsl_gpumem_get_info *info)
{
   int ret = safe_ioctl(fd, IOCTL_KGSL_GPUMEM_GET_INFO, info);
   if (ret == 0 || errno != EACCES)
      return ret;

   struct kgsl_gpuobj_info obj = {
      .id = info->id,
   };
   ret = safe_ioctl(fd, IOCTL_KGSL_GPUOBJ_INFO, &obj);
   if (ret)
      return ret;

   info->gpuaddr = obj.gpuaddr;
   info->flags = obj.flags;
   info->size = obj.size;
   info->mmapsize = obj.va_len;
   return 0;
}

static int
kgsl_free_id_compat(int fd, struct kgsl_gpumem_free_id *req)
{
   int ret = safe_ioctl(fd, IOCTL_KGSL_GPUMEM_FREE_ID, req);
   if (ret == 0 || errno != EACCES)
      return ret;

   struct kgsl_gpuobj_free obj = {
      .id = req->id,
   };
   return safe_ioctl(fd, IOCTL_KGSL_GPUOBJ_FREE, &obj);
}

static int
kgsl_gpumem_alloc_id_compat(int fd, struct kgsl_gpumem_alloc_id *req)
{
   int ret = safe_ioctl(fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, req);
   if (ret == 0 || errno != EACCES)
      return ret;

   struct kgsl_gpuobj_alloc obj = {
      .size = req->size,
      .flags = req->flags,
   };
   ret = safe_ioctl(fd, IOCTL_KGSL_GPUOBJ_ALLOC, &obj);
   if (ret)
      return ret;

   /* The ioctl doesn't return the GPU address, so we have to query it. */
   struct kgsl_gpuobj_info info = {
      .id = obj.id,
   };
   ret = safe_ioctl(fd, IOCTL_KGSL_GPUOBJ_INFO, &info);
   if (ret) {
      struct kgsl_gpuobj_free free_req = {
         .id = obj.id,
      };
      safe_ioctl(fd, IOCTL_KGSL_GPUOBJ_FREE, &free_req);
      return ret;
   }

   req->id = obj.id;
   req->flags = info.flags;
   req->mmapsize = obj.mmapsize;
   req->gpuaddr = info.gpuaddr;
   return 0;
}
''' + END + '\n'

# Remove an earlier version of the helpers, then redirect every remaining old call.
s = re.sub(re.escape(BEGIN) + r'.*?' + re.escape(END) + r'\n?', '', s, flags=re.S)
old_start = s.find('/* Horizon OS (Quest) refuses GPUMEM_ALLOC_ID with EACCES')
if old_start >= 0:
    old_end = s.index('\n}\n\n', old_start) + 4
    s = s[:old_start] + s[old_end:]

def redirect(ioctl, helper, text):
    pattern = r'safe_ioctl\(\s*([^,]+?),\s*' + ioctl + r',\s*(&?\w+)\s*\)'
    return re.sub(pattern, lambda m: f'{helper}({m[1]}, {m[2]})', text)

s = redirect('IOCTL_KGSL_GPUMEM_ALLOC_ID', 'kgsl_gpumem_alloc_id_compat', s)
s = redirect('IOCTL_KGSL_GPUMEM_GET_INFO', 'kgsl_get_info_compat', s)
s = redirect('IOCTL_KGSL_GPUMEM_FREE_ID', 'kgsl_free_id_compat', s)

i = s.index('kgsl_align_flag(uint64_t align)')
i = s.rindex('\n', 0, s.rindex('\n', 0, i)) + 1
s = s[:i] + HELPERS + s[i:]

for ioctl in ('GPUMEM_ALLOC_ID', 'GPUMEM_GET_INFO', 'GPUMEM_FREE_ID'):
    outside = s.replace(s[s.index(BEGIN):s.index(END)], '')
    if f'IOCTL_KGSL_{ioctl}' in outside:
        raise SystemExit(f'{ioctl} still called outside the helpers')
FILE.write_text(s, encoding='utf-8', newline='\n')
print('patched', FILE)
