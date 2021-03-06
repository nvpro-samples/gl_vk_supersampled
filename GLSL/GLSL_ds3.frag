#version 440 core
layout(set=0, binding=0) uniform sampler2D texImage;
layout(std140, set=0, binding=1) uniform texInfo {
   vec2 texelSize;
};
layout(location=1) in  vec2 tc0;
layout(location=0,index=0) out vec4 outColor;
void main()
{
	vec4 color, color2;
	vec4 tap0 = texture(texImage, tc0.xy);
	vec4 tap1 = texture(texImage, tc0.xy + texelSize * vec2(  0.4,  0.9 ));
	vec4 tap2 = texture(texImage, tc0.xy + texelSize * vec2( -0.4, -0.9 ));
	vec4 tap3 = texture(texImage, tc0.xy + texelSize * vec2( -0.9,  0.4 ));
	vec4 tap4 = texture(texImage, tc0.xy + texelSize * vec2(  0.9, -0.4 ));
	color = 0.2 * ( tap0 + tap1 + tap2 + tap3 + tap4 );
	vec4 tap11 = texture(texImage, tc0.xy + texelSize * vec2(  0.4,  0.9 ));
	vec4 tap21 = texture(texImage, tc0.xy + texelSize * vec2( -0.4, -0.9 ));
	vec4 tap31 = texture(texImage, tc0.xy + texelSize * vec2( -0.9,  0.4 ));
	vec4 tap41 = texture(texImage, tc0.xy + texelSize * vec2(  0.9, -0.4 ));
	color2 = 0.2 * ( tap0 + tap11 + tap21 + tap31 + tap41 );
	float mask = clamp(color2.w, 0.0, 1.0);
	outColor.rgb = color.rgb * mask + color2.rgb * (1.0-mask);														
	outColor.w = mask;																			
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