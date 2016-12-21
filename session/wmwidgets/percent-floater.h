/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __GRAPHENE_PERCENT_FLOATER_H__
#define __GRAPHENE_PERCENT_FLOATER_H__

#include <clutter/clutter.h>

G_BEGIN_DECLS

#define GRAPHENE_TYPE_PERCENT_FLOATER graphene_percent_floater_get_type()
G_DECLARE_FINAL_TYPE(GraphenePercentFloater, graphene_percent_floater, GRAPHENE, PERCENT_FLOATER, ClutterActor);

GraphenePercentFloater * graphene_percent_floater_new();
void graphene_percent_floater_set_divisions(GraphenePercentFloater *pf, guint divisions);
void graphene_percent_floater_set_scale(GraphenePercentFloater *pf, gfloat scale);
void graphene_percent_floater_set_percent(GraphenePercentFloater *pf, gfloat percent);
gfloat graphene_percent_floater_get_percent(GraphenePercentFloater *pf);

G_END_DECLS

#endif /* __GRAPHENE_PERCENT_FLOATER_H__ */
