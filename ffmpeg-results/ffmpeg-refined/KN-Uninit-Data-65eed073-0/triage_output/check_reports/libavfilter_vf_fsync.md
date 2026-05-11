### Report Summary

File:| filter/vf_fsync.c  
---|---  
Warning:| line 123, column 13  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


13    |  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
14    |  * Lesser General Public License for more details.
15    |  *
16    |  * You should have received a copy of the GNU Lesser General Public
17    |  * License along with FFmpeg; if not, write to the Free Software
18    |  * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
19    |  */
20    |  
21    | /**
22    |  * @file
23    |  * Filter for syncing video frames from external source
24    |  *
25    |  * @author Thilo Borgmann <thilo.borgmann _at_ mail.de>
26    |  */
27    |  
28    | #include "libavutil/avstring.h"
29    | #include "libavutil/error.h"
30    | #include "libavutil/mem.h"
31    | #include "libavutil/opt.h"
32    | #include "libavformat/avio.h"
33    | #include "video.h"
34    | #include "filters.h"
35    |  
36    | #define BUF_SIZE 256
37    |  
38    | typedef struct FsyncContext {
39    |  const AVClass *class;
40    |     AVIOContext *avio_ctx; // reading the map file
41    |     AVFrame *last_frame;   // buffering the last frame for duplicating eventually
42    |  char *filename;        // user-specified map file
43    |  char *buf;             // line buffer for the map file
44    |  char *cur;             // current position in the line buffer
45    |  char *end;             // end pointer of the line buffer
46    |     int64_t ptsi;          // input pts to map to [0-N] output pts
47    |     int64_t pts;           // output pts
48    |  int tb_num;            // output timebase num
49    |  int tb_den;            // output timebase den
50    | } FsyncContext;
51    |  
52    | #define OFFSET(x) offsetof(FsyncContext, x)
53    |  
54    | static const AVOption fsync_options[] = {
55    |     { "file",   "set the file name to use for frame sync", OFFSET(filename), AV_OPT_TYPE_STRING, { .str = "" }, .flags= AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM },
56    |     { "f",      "set the file name to use for frame sync", OFFSET(filename), AV_OPT_TYPE_STRING, { .str = "" }, .flags= AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM },
57    |     { NULL }
58    | };
59    |  
60    | /**
61    |  * Fills the buffer from cur to end, add \0 at EOF
62    |  */
63    | static int buf_fill(FsyncContext *ctx)
64    | {
65    |  int ret;
66    |  int num = ctx->end - ctx->cur;
67    |  
68    |     ret = avio_read(ctx->avio_ctx, ctx->cur, num);
69    |  if (ret < 0)
70    |  return ret;
71    |  if (ret < num) {
72    |         *(ctx->cur + ret) = '\0';
73    |     }
74    |  
75    |  return ret;
76    | }
77    |  
78    | /**
79    |  * Copies cur to end to the beginning and fills the rest
80    |  */
81    | static int buf_reload(FsyncContext *ctx)
82    | {
83    |  int i, ret;
84    |  int num = ctx->end - ctx->cur;
85    |  
86    |  for (i = 0; i < num; i++) {
87    |         ctx->buf[i] = *ctx->cur++;
88    |     }
89    |  
90    |     ctx->cur = ctx->buf + i;
91    |     ret = buf_fill(ctx);
92    |  if (ret < 0)
93    |  return ret;
94    |     ctx->cur = ctx->buf;
95    |  
96    |  return ret;
97    | }
98    |  
99    | /**
100   |  * Skip from cur over eol
101   |  */
102   | static void buf_skip_eol(FsyncContext *ctx)
103   | {
104   |  char *i;
105   |  for (i = ctx->cur; i < ctx->end; i++) {
106   |  if (*i != '\n')// && *i != '\r')
107   |  break;
108   |     }
109   |     ctx->cur = i;
110   | }
111   |  
112   | /**
113   |  * Get number of bytes from cur until eol
114   |  *
115   |  * @return >= 0 in case of success,
116   |  *         -1 in case there is no line ending before end of buffer
117   |  */
118   | static int buf_get_line_count(FsyncContext *ctx)
119   | {
120   |  int ret = 0;
121   |  char *i;
122   |  for (i = ctx->cur; i < ctx->end; i++, ret++) {
    7←Assuming 'i' is < field 'end'→
123   |  if (*i == '\0' || *i == '\n')
    8←buffer read by avio_read may be partially uninitialized
124   |  return ret;
125   |     }
126   |  
127   |  return -1;
128   | }
129   |  
130   | /**
131   |  * Get number of bytes from cur to '\0'
132   |  */
133   | static int buf_get_zero(FsyncContext *ctx)
134   | {
135   |  return av_strnlen(ctx->cur, ctx->end - ctx->cur);
136   | }
137   |  
138   | static int activate(AVFilterContext *ctx)
139   | {
140   |  FsyncContext *s       = ctx->priv;
141   |     AVFilterLink *inlink  = ctx->inputs[0];
142   |     AVFilterLink *outlink = ctx->outputs[0];
143   |  
144   |  int ret, line_count;
145   |  AVFrame *frame;
146   |  
147   |  FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);
    1Assuming 'ret' is 0→
    2←Taking false branch→
    3←Loop condition is false.  Exiting loop→
148   |  
149   |  buf_skip_eol(s);
150   |     line_count = buf_get_line_count(s);
151   |  if (line_count3.1'line_count' is < 0 < 0) {
    4←Taking true branch→
152   |  line_count = buf_reload(s);
153   |  if (line_count4.1'line_count' is >= 0 < 0)
    5←Taking false branch→
154   |  return line_count;
155   |  line_count = buf_get_line_count(s);
    6←Calling 'buf_get_line_count'→
156   |  if (line_count < 0)
157   |  return line_count;
158   |     }
159   |  
160   |  if (avio_feof(s->avio_ctx) && buf_get_zero(s) < 3) {
161   |         av_log(ctx, AV_LOG_DEBUG, "End of file. To zero = %i\n", buf_get_zero(s));
162   |  goto end;
163   |     }
164   |  
165   |  if (s->last_frame) {
166   |         ret = av_sscanf(s->cur, "%"PRId64" %"PRId64" %d/%d", &s->ptsi, &s->pts, &s->tb_num, &s->tb_den);
167   |  if (ret != 4) {
168   |             av_log(ctx, AV_LOG_ERROR, "Unexpected format found (%i / 4).\n", ret);
169   |             ff_outlink_set_status(outlink, AVERROR_INVALIDDATA, AV_NOPTS_VALUE);
170   |  return AVERROR_INVALIDDATA;
171   |         }
172   |  
173   |         av_log(ctx, AV_LOG_DEBUG, "frame %"PRId64" ", s->last_frame->pts);
174   |  
175   |  if (s->last_frame->pts >= s->ptsi) {
176   |             av_log(ctx, AV_LOG_DEBUG, ">= %"PRId64": DUP LAST with pts = %"PRId64"\n", s->ptsi, s->pts);
177   |  
178   |  // clone frame
179   |             frame = av_frame_clone(s->last_frame);
180   |  if (!frame) {
181   |                 ff_outlink_set_status(outlink, AVERROR(ENOMEM), AV_NOPTS_VALUE);
182   |  return AVERROR(ENOMEM);
183   |             }
184   |  
185   |  // set output pts and timebase