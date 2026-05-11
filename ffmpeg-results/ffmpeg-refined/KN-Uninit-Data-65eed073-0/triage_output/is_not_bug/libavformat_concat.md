### Report Summary

File:| format/concat.c  
---|---  
Warning:| line 245, column 14  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


162   | {
163   |     int64_t result;
164   |  struct concat_data  *data  = h->priv_data;
165   |  struct concat_nodes *nodes = data->nodes;
166   |     size_t i;
167   |  
168   |  if ((whence & AVSEEK_SIZE))
169   |  return data->total_size;
170   |  switch (whence) {
171   |  case SEEK_END:
172   |  for (i = data->length - 1; i && pos < -nodes[i].size; i--)
173   |             pos += nodes[i].size;
174   |  break;
175   |  case SEEK_CUR:
176   |  /* get the absolute position */
177   |  for (i = 0; i != data->current; i++)
178   |             pos += nodes[i].size;
179   |         pos += ffurl_seek(nodes[i].uc, 0, SEEK_CUR);
180   |         whence = SEEK_SET;
181   |  /* fall through with the absolute position */
182   |  case SEEK_SET:
183   |  for (i = 0; i != data->length - 1 && pos >= nodes[i].size; i++)
184   |             pos -= nodes[i].size;
185   |  break;
186   |  default:
187   |  return AVERROR(EINVAL);
188   |     }
189   |  
190   |     result = ffurl_seek(nodes[i].uc, pos, whence);
191   |  if (result >= 0) {
192   |         data->current = i;
193   |  while (i)
194   |             result += nodes[--i].size;
195   |     }
196   |  return result;
197   | }
198   |  
199   | #if CONFIG_CONCAT_PROTOCOL
200   | const URLProtocol ff_concat_protocol = {
201   |     .name           = "concat",
202   |     .url_open       = concat_open,
203   |     .url_read       = concat_read,
204   |     .url_seek       = concat_seek,
205   |     .url_close      = concat_close,
206   |     .priv_data_size = sizeof(struct concat_data),
207   |     .default_whitelist = "concat,file,subfile",
208   | };
209   | #endif
210   |  
211   | #if CONFIG_CONCATF_PROTOCOL
212   | static av_cold int concatf_open(URLContext *h, const char *uri, int flags)
213   | {
214   |  AVBPrint bp;
215   |  struct concat_data *data = h->priv_data;
216   |     AVIOContext *in = NULL;
217   |  const char *cursor;
218   |     int64_t total_size = 0;
219   |  unsigned int nodes_size = 0;
220   |     size_t i = 0;
221   |  int err;
222   |  
223   |  if (!av_strstart(uri, "concatf:", &uri)) {
    1Assuming the condition is false→
    2←Taking false branch→
224   |         av_log(h, AV_LOG_ERROR, "URL %s lacks prefix\n", uri);
225   |  return AVERROR(EINVAL);
226   |     }
227   |  
228   |  /* handle input */
229   |  if (!*uri)
    3←Assuming the condition is false→
    4←Taking false branch→
230   |  return AVERROR(ENOENT);
231   |  
232   |  err = ffio_open_whitelist(&in, uri, AVIO_FLAG_READ, &h->interrupt_callback,
233   |  NULL, h->protocol_whitelist, h->protocol_blacklist);
234   |  if (err < 0)
    5←Assuming 'err' is >= 0→
    6←Taking false branch→
235   |  return err;
236   |  
237   |  av_bprint_init(&bp, 0, AV_BPRINT_SIZE_UNLIMITED);
238   |     err = avio_read_to_bprint(in, &bp, SIZE_MAX);
239   |     avio_closep(&in);
240   |  if (err < 0) {
    7←Assuming 'err' is >= 0→
    8←Taking false branch→
241   |         av_bprint_finalize(&bp, NULL);
242   |  return err;
243   |     }
244   |  
245   |  cursor = bp.str;
    9←buffer read by avio_read may be partially uninitialized
246   |  while (*cursor) {
247   |  struct concat_nodes *nodes;
248   |         URLContext *uc;
249   |  char *node_uri;
250   |         int64_t size;
251   |         size_t len = i;
252   |  int leading_spaces = strspn(cursor, " \n\t\r");
253   |  
254   |  if (!cursor[leading_spaces])
255   |  break;
256   |  
257   |         node_uri = av_get_token(&cursor, "\r\n");
258   |  if (!node_uri) {
259   |             err = AVERROR(ENOMEM);
260   |  break;
261   |         }
262   |  if (*cursor)
263   |             cursor++;
264   |  
265   |  if (++len == SIZE_MAX / sizeof(*nodes)) {
266   |             av_free(node_uri);
267   |             err = AVERROR(ENAMETOOLONG);
268   |  break;
269   |         }
270   |  
271   |  /* creating URLContext */
272   |         err = ffurl_open_whitelist(&uc, node_uri, flags,
273   |                                    &h->interrupt_callback, NULL, h->protocol_whitelist, h->protocol_blacklist, h);
274   |         av_free(node_uri);
275   |  if (err < 0)