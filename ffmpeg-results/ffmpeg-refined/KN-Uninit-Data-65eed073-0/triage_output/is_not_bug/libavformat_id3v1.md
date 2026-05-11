### Report Summary

File:| format/id3v1.c  
---|---  
Warning:| line 260, column 11  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


206   |     [179] = "Space Rock",
207   |     [180] = "Trop Rock",
208   |     [181] = "World Music",
209   |     [182] = "Neoclassical",
210   |     [183] = "Audiobook",
211   |     [184] = "Audio Theatre",
212   |     [185] = "Neue Deutsche Welle",
213   |     [186] = "Podcast",
214   |     [187] = "Indie Rock",
215   |     [188] = "G-Funk",
216   |     [189] = "Dubstep",
217   |     [190] = "Garage Rock",
218   |     [191] = "Psybient"
219   | };
220   |  
221   | static void get_string(AVFormatContext *s, const char *key,
222   |  const uint8_t *buf, int buf_size)
223   | {
224   |  int i, c;
225   |  char *q, str[512], *first_free_space = NULL;
226   |  
227   |     q = str;
228   |  for(i = 0; i < buf_size; i++) {
229   |         c = buf[i];
230   |  if (c == '\0')
231   |  break;
232   |  if ((q - str) >= sizeof(str) - 1)
233   |  break;
234   |  if (c == ' ') {
235   |  if (!first_free_space)
236   |                 first_free_space = q;
237   |         } else {
238   |             first_free_space = NULL;
239   |         }
240   |         *q++ = c;
241   |     }
242   |     *q = '\0';
243   |  
244   |  if (first_free_space)
245   |         *first_free_space = '\0';
246   |  
247   |  if (*str)
248   |         av_dict_set(&s->metadata, key, str, 0);
249   | }
250   |  
251   | /**
252   |  * Parse an ID3v1 tag
253   |  *
254   |  * @param buf ID3v1_TAG_SIZE long buffer containing the tag
255   |  */
256   | static int parse_tag(AVFormatContext *s, const uint8_t *buf)
257   | {
258   |  int genre;
259   |  
260   |  if (!(buf[0] == 'T' &&
    8←buffer read by avio_read may be partially uninitialized
261   |           buf[1] == 'A' &&
262   |           buf[2] == 'G'))
263   |  return -1;
264   |     get_string(s, "title",   buf +  3, 30);
265   |     get_string(s, "artist",  buf + 33, 30);
266   |     get_string(s, "album",   buf + 63, 30);
267   |     get_string(s, "date",    buf + 93,  4);
268   |     get_string(s, "comment", buf + 97, 30);
269   |  if (buf[125] == 0 && buf[126] != 0) {
270   |         av_dict_set_int(&s->metadata, "track", buf[126], 0);
271   |     }
272   |     genre = buf[127];
273   |  if (genre <= ID3v1_GENRE_MAX)
274   |         av_dict_set(&s->metadata, "genre", ff_id3v1_genre_str[genre], 0);
275   |  return 0;
276   | }
277   |  
278   | void ff_id3v1_read(AVFormatContext *s)
279   | {
280   |  int ret;
281   |     uint8_t buf[ID3v1_TAG_SIZE];
282   |     int64_t filesize, position = avio_tell(s->pb);
283   |  
284   |  if (s->pb->seekable & AVIO_SEEKABLE_NORMAL) {
    1Assuming the condition is true→
    2←Taking true branch→
285   |  /* XXX: change that */
286   |  filesize = avio_size(s->pb);
287   |  if (filesize > 128) {
    3←Assuming 'filesize' is > 128→
    4←Taking true branch→
288   |  avio_seek(s->pb, filesize - 128, SEEK_SET);
289   |             ret = avio_read(s->pb, buf, ID3v1_TAG_SIZE);
290   |  if (ret == ID3v1_TAG_SIZE) {
    5←Assuming 'ret' is equal to ID3v1_TAG_SIZE→
    6←Taking true branch→
291   |  parse_tag(s, buf);
    7←Calling 'parse_tag'→
292   |             }
293   |             avio_seek(s->pb, position, SEEK_SET);
294   |         }
295   |     }
296   | }