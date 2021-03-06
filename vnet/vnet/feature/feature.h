/*
 * Copyright (c) 2016 Cisco and/or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef included_features_h
#define included_features_h

#include <vnet/vnet.h>

/** feature registration object */
typedef struct _vnet_feature_arc_registration
{
  /** next registration in list of all registrations*/
  struct _vnet_feature_arc_registration *next;
  /** Feature Arc name */
  char *arc_name;
  /** Start nodes */
  char **start_nodes;
  int n_start_nodes;
  /* Feature arc index, assigned by init function */
  u16 feature_arc_index;
} vnet_feature_arc_registration_t;

/** feature registration object */
typedef struct _vnet_feature_registration
{
  /** next registration in list of all registrations*/
  struct _vnet_feature_registration *next;
  /** Feature arc name */
  char *arc_name;
  /** Graph node name */
  char *node_name;
  /** Pointer to this feature index, filled in by vnet_feature_arc_init */
  u32 *feature_index;
  u32 feature_index_u32;
  /** Constraints of the form "this feature runs before X" */
  char **runs_before;
  /** Constraints of the form "this feature runs after Y" */
  char **runs_after;
} vnet_feature_registration_t;

typedef struct vnet_feature_config_main_t_
{
  vnet_config_main_t config_main;
  u32 *config_index_by_sw_if_index;
} vnet_feature_config_main_t;

typedef struct
{
  /** feature arc configuration list */
  vnet_feature_arc_registration_t *next_arc;
  uword **arc_index_by_name;

  /** feature path configuration lists */
  vnet_feature_registration_t *next_feature;
  vnet_feature_registration_t **next_feature_by_arc;
  uword **next_feature_by_name;

  /** feature config main objects */
  vnet_feature_config_main_t *feature_config_mains;

  /** Save partial order results for show command */
  char ***feature_nodes;

  /** bitmap of interfaces which have driver rx features configured */
  uword **sw_if_index_has_features;

  /** feature reference counts by interface */
  i16 **feature_count_by_sw_if_index;

  /** convenience */
  vlib_main_t *vlib_main;
  vnet_main_t *vnet_main;
} vnet_feature_main_t;

extern vnet_feature_main_t feature_main;

#define VNET_FEATURE_ARC_INIT(x,...)				\
  __VA_ARGS__ vnet_feature_arc_registration_t vnet_feat_arc_##x;\
static void __vnet_add_feature_arc_registration_##x (void)	\
  __attribute__((__constructor__)) ;				\
static void __vnet_add_feature_arc_registration_##x (void)	\
{								\
  vnet_feature_main_t * fm = &feature_main;			\
  vnet_feat_arc_##x.next = fm->next_arc;			\
  fm->next_arc = & vnet_feat_arc_##x;				\
}								\
__VA_ARGS__ vnet_feature_arc_registration_t vnet_feat_arc_##x

#define VNET_FEATURE_INIT(x,...)				\
  __VA_ARGS__ vnet_feature_registration_t vnet_feat_##x;	\
static void __vnet_add_feature_registration_##x (void)		\
  __attribute__((__constructor__)) ;				\
static void __vnet_add_feature_registration_##x (void)		\
{								\
  vnet_feature_main_t * fm = &feature_main;			\
  vnet_feat_##x.next = fm->next_feature;			\
  fm->next_feature = & vnet_feat_##x;				\
}								\
__VA_ARGS__ vnet_feature_registration_t vnet_feat_##x

void
vnet_config_update_feature_count (vnet_feature_main_t * fm, u16 arc,
				  u32 sw_if_index, int is_add);

u32 vnet_feature_index_from_node_name (u16 type, const char *s);

void
vnet_feature_enable_disable (const char *arc_name, const char *node_name,
			     u32 sw_if_index, int enable_disable,
			     void *feature_config,
			     u32 n_feature_config_bytes);


static_always_inline int
vnet_have_features (u32 arc, u32 sw_if_index)
{
  vnet_feature_main_t *fm = &feature_main;
  return clib_bitmap_get (fm->sw_if_index_has_features[arc], sw_if_index);
}

static_always_inline u32
vnet_feature_get_config_index (u16 arc, u32 sw_if_index)
{
  vnet_feature_main_t *fm = &feature_main;
  vnet_feature_config_main_t *cm = &fm->feature_config_mains[arc];
  return vec_elt (cm->config_index_by_sw_if_index, sw_if_index);
}

static_always_inline void
vnet_feature_redirect (u16 arc, u32 sw_if_index, u32 * next0,
		       vlib_buffer_t * b0)
{
  vnet_feature_main_t *fm = &feature_main;
  vnet_feature_config_main_t *cm = &fm->feature_config_mains[arc];

  if (PREDICT_FALSE (vnet_have_features (arc, sw_if_index)))
    {
      b0->current_config_index =
	vec_elt (cm->config_index_by_sw_if_index, sw_if_index);
      vnet_get_config_data (&cm->config_main, &b0->current_config_index,
			    next0, /* # bytes of config data */ 0);
    }
}

static_always_inline void
vnet_feature_device_input_redirect_x1 (vlib_node_runtime_t * node,
				       u32 sw_if_index, u32 * next0,
				       vlib_buffer_t * b0,
				       u16 buffer_advanced0)
{
  vnet_feature_main_t *fm = &feature_main;
  vnet_feature_config_main_t *cm;

  ASSERT (node->feature_arc_index != ~(u16) 0);
  cm = &fm->feature_config_mains[node->feature_arc_index];

  if (PREDICT_FALSE
      (clib_bitmap_get
       (fm->sw_if_index_has_features[node->feature_arc_index], sw_if_index)))
    {
      /*
       * Save next0 so that the last feature in the chain
       * can skip ethernet-input if indicated...
       */
      vnet_buffer (b0)->device_input_feat.saved_next_index = *next0;
      vnet_buffer (b0)->device_input_feat.buffer_advance = buffer_advanced0;
      vlib_buffer_advance (b0, -buffer_advanced0);

      b0->current_config_index =
	vec_elt (cm->config_index_by_sw_if_index, sw_if_index);
      vnet_get_config_data (&cm->config_main, &b0->current_config_index,
			    next0, /* # bytes of config data */ 0);
    }
}

#define ORDER_CONSTRAINTS (char*[])
#define VNET_FEATURES(...)  (char*[]) { __VA_ARGS__, 0}

clib_error_t *vnet_feature_arc_init (vlib_main_t * vm,
				     vnet_config_main_t * vcm,
				     char **feature_start_nodes,
				     int num_feature_start_nodes,
				     vnet_feature_registration_t *
				     first_reg, char ***feature_nodes);

void vnet_interface_features_show (vlib_main_t * vm, u32 sw_if_index);

#endif /* included_feature_h */

/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */
