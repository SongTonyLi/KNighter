### Report Summary

File:| avfilter/perlin.c  
---|---  
Warning:| line 222, column 18  
division by possibly zero aggregate factor  
  
### Annotated Source Code


158   |  // Calculate the "unit cube" that the point asked will be located in
159   |  // The left bound is ( |_x_|,|_y_|,|_z_| ) and the right bound is that
160   |  // plus 1.  Next we calculate the location (from 0.0 to 1.0) in that cube.
161   |     xi = (int)x & 255;
162   |     yi = (int)y & 255;
163   |     zi = (int)z & 255;
164   |  
165   |     xf = x - (int)x;
166   |     yf = y - (int)y;
167   |     zf = z - (int)z;
168   |  
169   |  // We also fade the location to smooth the result.
170   |     u = fade(xf);
171   |     v = fade(yf);
172   |     w = fade(zf);
173   |  
174   |     aaa = p[p[p[    xi         ] +     yi         ] +     zi         ];
175   |     aba = p[p[p[    xi         ] + inc(yi, period)] +     zi         ];
176   |     aab = p[p[p[    xi         ] +     yi         ] + inc(zi, period)];
177   |     abb = p[p[p[    xi         ] + inc(yi, period)] + inc(zi, period)];
178   |     baa = p[p[p[inc(xi, period)] +     yi         ] +     zi         ];
179   |     bba = p[p[p[inc(xi, period)] + inc(yi, period)] +     zi         ];
180   |     bab = p[p[p[inc(xi, period)] +     yi         ] + inc(zi, period)];
181   |     bbb = p[p[p[inc(xi, period)] + inc(yi, period)] + inc(zi, period)];
182   |  
183   |  // The gradient function calculates the dot product between a pseudorandom
184   |  // gradient vector and the vector from the input coordinate to the 8
185   |  // surrounding points in its unit cube.
186   |  // This is all then lerped together as a sort of weighted average based on the faded (u,v,w)
187   |  // values we made earlier.
188   |     x1 = lerp(grad(aaa, xf  , yf  , zf),
189   |               grad(baa, xf-1, yf  , zf),
190   |               u);
191   |     x2 = lerp(grad(aba, xf  , yf-1, zf),
192   |               grad(bba, xf-1, yf-1, zf),
193   |               u);
194   |     y1 = lerp(x1, x2, v);
195   |  
196   |     x1 = lerp(grad(aab, xf  , yf  , zf-1),
197   |               grad(bab, xf-1, yf  , zf-1),
198   |               u);
199   |     x2 = lerp(grad(abb, xf  , yf-1, zf-1),
200   |               grad(bbb, xf-1, yf-1, zf-1),
201   |                     u);
202   |     y2 = lerp(x1, x2, v);
203   |  
204   |  // For convenience we bound it to 0 - 1 (theoretical min/max before is -1 - 1)
205   |  return (lerp(y1, y2, w) + 1) / 2;
206   | }
207   |  
208   | double ff_perlin_get(FFPerlin *perlin, double x, double y, double z)
209   | {
210   |  double total = 0;
211   |  double frequency = 1;
212   |  double amplitude = 1;
213   |  double max_value = 0;                   // Used for normalizing result to 0.0 - 1.0
214   |  
215   |  for (int i = 0; i < perlin->octaves; i++) {
    1Assuming 'i' is >= field 'octaves'→
    2←Loop condition is false. Execution continues on line 222→
216   |         total += perlin_get(perlin, x * frequency, y * frequency, z * frequency) * amplitude;
217   |         max_value += amplitude;
218   |         amplitude *= perlin->persistence;
219   |         frequency *= 2;
220   |     }
221   |  
222   |  return total / max_value;
    3←division by possibly zero aggregate factor
223   | }