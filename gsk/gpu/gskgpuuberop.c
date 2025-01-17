#include "config.h"

#include "gskgpuuberopprivate.h"

#include "gskgpuframeprivate.h"
#include "gskgpuprintprivate.h"
#include "gskgpushaderopprivate.h"
#include "gskrectprivate.h"

#include "gpu/shaders/gskgpuuberinstance.h"

typedef struct _GskGpuUberOp GskGpuUberOp;

struct _GskGpuUberOp
{
  GskGpuShaderOp op;
};

static void
gsk_gpu_uber_op_print (GskGpuOp    *op,
                       GskGpuFrame *frame,
                       GString     *string,
                       guint        indent)
{
  GskGpuShaderOp *shader = (GskGpuShaderOp *) op;
  GskGpuUberInstance *instance;

  instance = (GskGpuUberInstance *) gsk_gpu_frame_get_vertex_data (frame, shader->vertex_offset);

  gsk_gpu_print_op (string, indent, "uber");
  gsk_gpu_print_shader_info (string, shader->clip);
  gsk_gpu_print_rect (string, instance->rect);
  gsk_gpu_print_newline (string);
}

static const GskGpuShaderOpClass GSK_GPU_UBER_OP_CLASS = {
  {
    GSK_GPU_OP_SIZE (GskGpuUberOp),
    GSK_GPU_STAGE_SHADER,
    gsk_gpu_shader_op_finish,
    gsk_gpu_uber_op_print,
#ifdef GDK_RENDERING_VULKAN
    gsk_gpu_shader_op_vk_command,
#endif
    gsk_gpu_shader_op_gl_command
  },
  "gskgpuuber",
  sizeof (GskGpuUberInstance),
#ifdef GDK_RENDERING_VULKAN
  &gsk_gpu_uber_info,
#endif
  gsk_gpu_uber_setup_attrib_locations,
  gsk_gpu_uber_setup_vao
};

void
gsk_gpu_uber_op (GskGpuFrame             *frame,
                 GskGpuShaderClip         clip,
                 const graphene_rect_t   *rect,
                 const graphene_point_t  *offset,
                 GskGpuDescriptors       *desc,
                 guint32                  pattern_id)
{
  GskGpuUberInstance *instance;

  gsk_gpu_shader_op_alloc (frame,
                           &GSK_GPU_UBER_OP_CLASS,
                           0,
                           clip,
                           desc,
                           &instance);

  gsk_gpu_rect_to_float (rect, offset, instance->rect);
  instance->pattern_id = pattern_id;
}
