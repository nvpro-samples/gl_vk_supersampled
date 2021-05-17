#version 440 core
layout(set=0, binding=0) uniform sampler2D texImage;
//layout(set=0, binding = 0, rgba8) uniform imageBuffer im;
layout(std140, set=0, binding=1) uniform texInfo {
   vec2 texelSize;
};
layout(location=1) in  vec2 tc0;
layout(location=0,index=0) out vec4 outColor;
void main()
{
	outColor = texture(texImage, tc0.xy);
}

/*
 * Copyright (c) 2016-2021, NVIDIA CORPORATION.  All rights reserved.
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
 *
 * SPDX-FileCopyrightText: Copyright (c) 2016-2021 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */