### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/wtvdec.c  
---|---  
Warning:| line 292, column 48  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


12    |  * FFmpeg is distributed in the hope that it will be useful,
13    |  * but WITHOUT ANY WARRANTY; without even the implied warranty of
14    |  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
15    |  * Lesser General Public License for more details.
16    |  *
17    |  * You should have received a copy of the GNU Lesser General Public
18    |  * License along with FFmpeg; if not, write to the Free Software
19    |  * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
20    |  */
21    |  
22    | /**
23    |  * @file
24    |  * Windows Television (WTV) demuxer
25    |  * @author Peter Ross <pross@xvid.org>
26    |  */
27    |  
28    | #include <inttypes.h>
29    |  
30    | #include "libavutil/channel_layout.h"
31    | #include "libavutil/intreadwrite.h"
32    | #include "libavutil/intfloat.h"
33    | #include "libavutil/time_internal.h"
34    | #include "avformat.h"
35    | #include "internal.h"
36    | #include "wtv.h"
37    | #include "mpegts.h"
38    |  
39    | /* Macros for formatting GUIDs */
40    | #define PRI_PRETTY_GUID \
41    |  "%08"PRIx32"-%04"PRIx16"-%04"PRIx16"-%02x%02x%02x%02x%02x%02x%02x%02x"
42    | #define ARG_PRETTY_GUID(g) \
43    |  AV_RL32(g),AV_RL16(g+4),AV_RL16(g+6),g[8],g[9],g[10],g[11],g[12],g[13],g[14],g[15]
44    | #define LEN_PRETTY_GUID 34
45    |  
46    | /*
47    |  * File system routines
48    |  */
49    |  
50    | typedef struct WtvFile {
51    |     AVIOContext *pb_filesystem;  /**< file system (AVFormatContext->pb) */
52    |  
53    |  int sector_bits;     /**< sector shift bits; used to convert sector number into pb_filesystem offset */
54    |     uint32_t *sectors;   /**< file allocation table */
55    |  int nb_sectors;      /**< number of sectors */
56    |  
57    |  int error;
58    |     int64_t position;
59    |     int64_t length;
60    | } WtvFile;
61    |  
62    | static int64_t seek_by_sector(AVIOContext *pb, int64_t sector, int64_t offset)
63    | {
64    |  return avio_seek(pb, (sector << WTV_SECTOR_BITS) + offset, SEEK_SET);
65    | }
66    |  
67    | /**
68    |  * @return bytes read, AVERROR_EOF on end of file, or <0 on error
69    |  */
70    | static int wtvfile_read_packet(void *opaque, uint8_t *buf, int buf_size)
71    | {
72    |     WtvFile *wf = opaque;
73    |     AVIOContext *pb = wf->pb_filesystem;
74    |  int nread = 0, n = 0;
75    |  
76    |  if (wf->error || pb->error)
77    |  return -1;
78    |  if (wf->position >= wf->length || avio_feof(pb))
79    |  return AVERROR_EOF;
80    |  
81    |     buf_size = FFMIN(buf_size, wf->length - wf->position);
82    |  while(nread < buf_size) {
83    |  int remaining_in_sector = (1 << wf->sector_bits) - (wf->position & ((1 << wf->sector_bits) - 1));
84    |  int read_request        = FFMIN(buf_size - nread, remaining_in_sector);
85    |  
86    |         n = avio_read(pb, buf, read_request);
87    |  if (n <= 0)
88    |  break;
89    |         nread += n;
90    |         buf += n;
91    |         wf->position += n;
92    |  if (n == remaining_in_sector) {
93    |  int i = wf->position >> wf->sector_bits;
94    |  if (i >= wf->nb_sectors ||
205   |         av_freep(&wf);
206   |  return NULL;
207   |     }
208   |  
209   |     size = avio_size(s->pb);
210   |  if (size >= 0 && (int64_t)wf->sectors[wf->nb_sectors - 1] << WTV_SECTOR_BITS > size)
211   |         av_log(s, AV_LOG_WARNING, "truncated file\n");
212   |  
213   |  /* check length */
214   |     length &= 0xFFFFFFFFFFFF;
215   |  if (length > ((int64_t)wf->nb_sectors << wf->sector_bits)) {
216   |         av_log(s, AV_LOG_WARNING, "reported file length (0x%"PRIx64") exceeds number of available sectors (0x%"PRIx64")\n", length, (int64_t)wf->nb_sectors << wf->sector_bits);
217   |         length = (int64_t)wf->nb_sectors <<  wf->sector_bits;
218   |     }
219   |     wf->length = length;
220   |  
221   |  /* seek to initial sector */
222   |     wf->position = 0;
223   |  if (seek_by_sector(s->pb, wf->sectors[0], 0) < 0) {
224   |         av_freep(&wf->sectors);
225   |         av_freep(&wf);
226   |  return NULL;
227   |     }
228   |  
229   |     wf->pb_filesystem = s->pb;
230   |     buffer = av_malloc(1 << wf->sector_bits);
231   |  if (!buffer) {
232   |         av_freep(&wf->sectors);
233   |         av_freep(&wf);
234   |  return NULL;
235   |     }
236   |  
237   |     pb = avio_alloc_context(buffer, 1 << wf->sector_bits, 0, wf,
238   |                            wtvfile_read_packet, NULL, wtvfile_seek);
239   |  if (!pb) {
240   |         av_freep(&buffer);
241   |         av_freep(&wf->sectors);
242   |         av_freep(&wf);
243   |     }
244   |  return pb;
245   | }
246   |  
247   | /**
248   |  * Open file using filename
249   |  * @param[in]  buf       directory buffer
250   |  * @param      buf_size  directory buffer size
251   |  * @param[in]  filename
252   |  * @param      filename_size size of filename
253   |  * @return NULL on error
254   |  */
255   | static AVIOContext * wtvfile_open2(AVFormatContext *s, const uint8_t *buf, int buf_size, const uint8_t *filename, int filename_size)
256   | {
257   |  const uint8_t *buf_end = buf + buf_size;
258   |  
259   |  while(buf + 48 <= buf_end) {
    8←Assuming the condition is true→
    9←Loop condition is true.  Entering loop body→
260   |  int dir_length, name_size, first_sector, depth;
261   |         uint64_t file_length;
262   |  const uint8_t *name;
263   |  if (ff_guidcmp(buf, ff_dir_entry_guid)) {
    10←Assuming the condition is false→
    11←Taking false branch→
264   |             av_log(s, AV_LOG_ERROR, "unknown guid "FF_PRI_GUID", expected dir_entry_guid; "
265   |  "remaining directory entries ignored\n", FF_ARG_GUID(buf));
266   |  break;
267   |         }
268   |  dir_length  = AV_RL16(buf + 16);
269   |         file_length = AV_RL64(buf + 24);
270   |         name_size   = 2 * AV_RL32(buf + 32);
271   |  if (name_size < 0) {
    12←Assuming 'name_size' is >= 0→
    13←Taking false branch→
272   |             av_log(s, AV_LOG_ERROR,
273   |  "bad filename length, remaining directory entries ignored\n");
274   |  break;
275   |         }
276   |  if (dir_length == 0) {
    14←Assuming 'dir_length' is not equal to 0→
    15←Taking false branch→
277   |             av_log(s, AV_LOG_ERROR,
278   |  "bad dir length, remaining directory entries ignored\n");
279   |  break;
280   |         }
281   |  if (48 + (int64_t)name_size > buf_end - buf) {
    16←Assuming the condition is false→
    17←Taking false branch→
282   |             av_log(s, AV_LOG_ERROR, "filename exceeds buffer size; remaining directory entries ignored\n");
283   |  break;
284   |         }
285   |  first_sector = AV_RL32(buf + 40 + name_size);
286   |         depth        = AV_RL32(buf + 44 + name_size);
287   |  
288   |  /* compare file name; test optional null terminator */
289   |         name = buf + 40;
290   |  if (name_size >= filename_size &&
    18←Assuming 'name_size' is >= 'filename_size'→
291   |  !memcmp(name, filename, filename_size) &&
    19←Assuming the condition is true→
292   |             (name_size < filename_size + 2 || !AV_RN16(name + filename_size)))
    20←Assuming the condition is false→
    21←buffer read by avio_read may be partially uninitialized
293   |  return wtvfile_open_sector(first_sector, file_length, depth, s);
294   |  
295   |         buf += dir_length;
296   |     }
297   |  return NULL;
298   | }
299   |  
300   | #define wtvfile_open(s, buf, buf_size, filename) \
301   |  wtvfile_open2(s, buf, buf_size, filename, sizeof(filename))
302   |  
303   | /**
304   |  * Close file opened with wtvfile_open_sector(), or wtv_open()
305   |  */
306   | static void wtvfile_close(AVIOContext *pb)
307   | {
308   |     WtvFile *wf = pb->opaque;
309   |     av_freep(&wf->sectors);
310   |     av_freep(&pb->opaque);
311   |     av_freep(&pb->buffer);
312   |     avio_context_free(&pb);
313   | }
314   |  
315   | /*
316   |  * Main demuxer
317   |  */
318   |  
319   | typedef struct WtvStream {
320   |  int seen_data;
321   | } WtvStream;
322   |  
910   |                     }
911   |                 }
912   |             }
913   |         } else if (!ff_guidcmp(g, ff_data_guid)) {
914   |  int stream_index = ff_find_stream_index(s, sid);
915   |  if (mode == SEEK_TO_DATA && stream_index >= 0 && len > 32 && s->streams[stream_index]->priv_data) {
916   |                 WtvStream *wst = s->streams[stream_index]->priv_data;
917   |                 wst->seen_data = 1;
918   |  if (len_ptr) {
919   |                     *len_ptr = len;
920   |                 }
921   |  return stream_index;
922   |             }
923   |         } else if (!ff_guidcmp(g, /* DSATTRIB_WMDRMProtectionInfo */ (const ff_asf_guid){0x83,0x95,0x74,0x40,0x9D,0x6B,0xEC,0x4E,0xB4,0x3C,0x67,0xA1,0x80,0x1E,0x1A,0x9B})) {
924   |  int stream_index = ff_find_stream_index(s, sid);
925   |  if (stream_index >= 0)
926   |                 av_log(s, AV_LOG_WARNING, "encrypted stream detected (st:%d), decoding will likely fail\n", stream_index);
927   |         } else if (
928   |             !ff_guidcmp(g, /* DSATTRIB_CAPTURE_STREAMTIME */ (const ff_asf_guid){0x14,0x56,0x1A,0x0C,0xCD,0x30,0x40,0x4F,0xBC,0xBF,0xD0,0x3E,0x52,0x30,0x62,0x07}) ||
929   |             !ff_guidcmp(g, /* DSATTRIB_PBDATAG_ATTRIBUTE */ (const ff_asf_guid){0x79,0x66,0xB5,0xE0,0xB9,0x12,0xCC,0x43,0xB7,0xDF,0x57,0x8C,0xAA,0x5A,0x7B,0x63}) ||
930   |             !ff_guidcmp(g, /* DSATTRIB_PicSampleSeq */ (const ff_asf_guid){0x02,0xAE,0x5B,0x2F,0x8F,0x7B,0x60,0x4F,0x82,0xD6,0xE4,0xEA,0x2F,0x1F,0x4C,0x99}) ||
931   |             !ff_guidcmp(g, /* DSATTRIB_TRANSPORT_PROPERTIES */ ff_DSATTRIB_TRANSPORT_PROPERTIES) ||
932   |             !ff_guidcmp(g, /* dvr_ms_vid_frame_rep_data */ (const ff_asf_guid){0xCC,0x32,0x64,0xDD,0x29,0xE2,0xDB,0x40,0x80,0xF6,0xD2,0x63,0x28,0xD2,0x76,0x1F}) ||
933   |             !ff_guidcmp(g, /* EVENTID_ChannelChangeSpanningEvent */ (const ff_asf_guid){0xE5,0xC5,0x67,0x90,0x5C,0x4C,0x05,0x42,0x86,0xC8,0x7A,0xFE,0x20,0xFE,0x1E,0xFA}) ||
934   |             !ff_guidcmp(g, /* EVENTID_ChannelInfoSpanningEvent */ (const ff_asf_guid){0x80,0x6D,0xF3,0x41,0x32,0x41,0xC2,0x4C,0xB1,0x21,0x01,0xA4,0x32,0x19,0xD8,0x1B}) ||
935   |             !ff_guidcmp(g, /* EVENTID_ChannelTypeSpanningEvent */ (const ff_asf_guid){0x51,0x1D,0xAB,0x72,0xD2,0x87,0x9B,0x48,0xBA,0x11,0x0E,0x08,0xDC,0x21,0x02,0x43}) ||
936   |             !ff_guidcmp(g, /* EVENTID_PIDListSpanningEvent */ (const ff_asf_guid){0x65,0x8F,0xFC,0x47,0xBB,0xE2,0x34,0x46,0x9C,0xEF,0xFD,0xBF,0xE6,0x26,0x1D,0x5C}) ||
937   |             !ff_guidcmp(g, /* EVENTID_SignalAndServiceStatusSpanningEvent */ (const ff_asf_guid){0xCB,0xC5,0x68,0x80,0x04,0x3C,0x2B,0x49,0xB4,0x7D,0x03,0x08,0x82,0x0D,0xCE,0x51}) ||
938   |             !ff_guidcmp(g, /* EVENTID_StreamTypeSpanningEvent */ (const ff_asf_guid){0xBC,0x2E,0xAF,0x82,0xA6,0x30,0x64,0x42,0xA8,0x0B,0xAD,0x2E,0x13,0x72,0xAC,0x60}) ||
939   |             !ff_guidcmp(g, (const ff_asf_guid){0x1E,0xBE,0xC3,0xC5,0x43,0x92,0xDC,0x11,0x85,0xE5,0x00,0x12,0x3F,0x6F,0x73,0xB9}) ||
940   |             !ff_guidcmp(g, (const ff_asf_guid){0x3B,0x86,0xA2,0xB1,0xEB,0x1E,0xC3,0x44,0x8C,0x88,0x1C,0xA3,0xFF,0xE3,0xE7,0x6A}) ||
941   |             !ff_guidcmp(g, (const ff_asf_guid){0x4E,0x7F,0x4C,0x5B,0xC4,0xD0,0x38,0x4B,0xA8,0x3E,0x21,0x7F,0x7B,0xBF,0x52,0xE7}) ||
942   |             !ff_guidcmp(g, (const ff_asf_guid){0x63,0x36,0xEB,0xFE,0xA1,0x7E,0xD9,0x11,0x83,0x08,0x00,0x07,0xE9,0x5E,0xAD,0x8D}) ||
943   |             !ff_guidcmp(g, (const ff_asf_guid){0x70,0xE9,0xF1,0xF8,0x89,0xA4,0x4C,0x4D,0x83,0x73,0xB8,0x12,0xE0,0xD5,0xF8,0x1E}) ||
944   |             !ff_guidcmp(g, ff_index_guid) ||
945   |             !ff_guidcmp(g, ff_sync_guid) ||
946   |             !ff_guidcmp(g, ff_stream1_guid) ||
947   |             !ff_guidcmp(g, (const ff_asf_guid){0xF7,0x10,0x02,0xB9,0xEE,0x7C,0xED,0x4E,0xBD,0x7F,0x05,0x40,0x35,0x86,0x18,0xA1})) {
948   |  //ignore known guids
949   |         } else
950   |             av_log(s, AV_LOG_WARNING, "unsupported chunk:"FF_PRI_GUID"\n", FF_ARG_GUID(g));
951   |  
952   |  if (avio_feof(pb))
953   |  break;
954   |  
955   |         avio_skip(pb, WTV_PAD8(len) - consumed);
956   |     }
957   |  return AVERROR_EOF;
958   | }
959   |  
960   | static int read_header(AVFormatContext *s)
961   | {
962   |  WtvContext *wtv = s->priv_data;
963   |  unsigned root_sector;
964   |  int root_size;
965   |     uint8_t root[WTV_SECTOR_SIZE];
966   |     AVIOContext *pb;
967   |     int64_t timeline_pos;
968   |     int64_t ret;
969   |  
970   |     wtv->epoch          =
971   |     wtv->pts            =
972   |     wtv->last_valid_pts = AV_NOPTS_VALUE;
973   |  
974   |  /* read root directory sector */
975   |     avio_skip(s->pb, 0x30);
976   |     root_size = avio_rl32(s->pb);
977   |  if (root_size > sizeof(root)) {
    1Assuming the condition is false→
    2←Taking false branch→
978   |         av_log(s, AV_LOG_ERROR, "root directory size exceeds sector size\n");
979   |  return AVERROR_INVALIDDATA;
980   |     }
981   |  avio_skip(s->pb, 4);
982   |     root_sector = avio_rl32(s->pb);
983   |  
984   |     ret = seek_by_sector(s->pb, root_sector, 0);
985   |  if (ret < 0)
    3←Assuming 'ret' is >= 0→
    4←Taking false branch→
986   |  return ret;
987   |  root_size = avio_read(s->pb, root, root_size);
988   |  if (root_size < 0)
    5←Assuming 'root_size' is >= 0→
    6←Taking false branch→
989   |  return AVERROR_INVALIDDATA;
990   |  
991   |  /* parse chunks up until first data chunk */
992   |  wtv->pb = wtvfile_open(s, root, root_size, ff_timeline_le16);
    7←Calling 'wtvfile_open2'→
993   |  if (!wtv->pb) {
994   |         av_log(s, AV_LOG_ERROR, "timeline data missing\n");
995   |  return AVERROR_INVALIDDATA;
996   |     }
997   |  
998   |     ret = parse_chunks(s, SEEK_TO_DATA, 0, 0);
999   |  if (ret < 0) {
1000  |         wtvfile_close(wtv->pb);
1001  |  return ret;
1002  |     }
1003  |     avio_seek(wtv->pb, -32, SEEK_CUR);
1004  |  
1005  |     timeline_pos = avio_tell(s->pb); // save before opening another file
1006  |  
1007  |  /* read metadata */
1008  |     pb = wtvfile_open(s, root, root_size, ff_table_0_entries_legacy_attrib_le16);
1009  |  if (pb) {
1010  |         parse_legacy_attrib(s, pb);
1011  |         wtvfile_close(pb);
1012  |     }
1013  |  
1014  |     s->ctx_flags |= AVFMTCTX_NOHEADER; // Needed for noStreams.wtv
1015  |  
1016  |  /* read seek index */
1017  |  if (s->nb_streams) {
1018  |         AVStream *st = s->streams[0];
1019  |         pb = wtvfile_open(s, root, root_size, ff_table_0_entries_time_le16);
1020  |  if (pb) {
1021  |  while(1) {
1022  |                 uint64_t timestamp = avio_rl64(pb);