### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/./libavcodec/bytestream.h  
---|---  
Warning:| line 96, column 1  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


1     | /*
2     |  * HCA demuxer
3     |  * Copyright (c) 2020 Paul B Mahol
4     |  *
5     |  * This file is part of FFmpeg.
6     |  *
7     |  * FFmpeg is free software; you can redistribute it and/or
8     |  * modify it under the terms of the GNU Lesser General Public
9     |  * License as published by the Free Software Foundation; either
10    |  * version 2.1 of the License, or (at your option) any later version.
11    |  *
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
22    | #include "libavutil/intreadwrite.h"
23    | #include "libavcodec/bytestream.h"
24    |  
25    | #include "avformat.h"
26    | #include "internal.h"
27    |  
28    | static int hca_probe(const AVProbeData *p)
29    | {
30    |  if (AV_RL32(p->buf) != MKTAG('H', 'C', 'A', 0))
31    |  return 0;
32    |  
33    |  if (AV_RL32(p->buf + 8) != MKTAG('f', 'm', 't', 0))
34    |  return 0;
35    |  
36    |  return AVPROBE_SCORE_MAX / 3;
37    | }
38    |  
39    | static int hca_read_header(AVFormatContext *s)
40    | {
41    |  AVCodecParameters *par;
42    |     GetByteContext gb;
43    |     AVIOContext *pb = s->pb;
44    |     AVStream *st;
45    |     uint32_t chunk;
46    |     uint16_t version;
47    |     uint32_t block_count;
48    |     uint16_t block_size;
49    |  int ret;
50    |  
51    |     avio_skip(pb, 4);
52    |     version = avio_rb16(pb);
53    |  
54    |     s->internal->data_offset = avio_rb16(pb);
55    |  if (s->internal->data_offset <= 8)
    1Assuming field 'data_offset' is > 8→
    2←Taking false branch→
56    |  return AVERROR_INVALIDDATA;
57    |  
58    |  st = avformat_new_stream(s, NULL);
59    |  if (!st)
    3←Assuming 'st' is non-null→
    4←Taking false branch→
60    |  return AVERROR(ENOMEM);
61    |  
62    |  par = st->codecpar;
63    |     ret = ff_alloc_extradata(par, s->internal->data_offset);
64    |  if (ret < 0)
    5←Assuming 'ret' is >= 0→
    6←Taking false branch→
65    |  return ret;
66    |  
67    |  ret = avio_read(pb, par->extradata + 8, par->extradata_size - 8);
68    |  if (ret < par->extradata_size - 8)
    7←Assuming the condition is false→
    8←Taking false branch→
69    |  return AVERROR(EIO);
70    |  AV_WL32(par->extradata, MKTAG('H', 'C', 'A', 0));
71    |  AV_WB16(par->extradata + 4, version);
72    |  AV_WB16(par->extradata + 6, s->internal->data_offset);
73    |  
74    |     bytestream2_init(&gb, par->extradata + 8, par->extradata_size - 8);
75    |  
76    |  if (bytestream2_get_le32(&gb) != MKTAG('f', 'm', 't', 0))
    9←Assuming the condition is false→
    10←Taking false branch→
77    |  return AVERROR_INVALIDDATA;
78    |  
79    |  par->codec_type  = AVMEDIA_TYPE_AUDIO;
80    |     par->codec_id    = AV_CODEC_ID_HCA;
81    |     par->codec_tag   = 0;
82    |     par->channels    = bytestream2_get_byte(&gb);
83    |     par->sample_rate = bytestream2_get_be24(&gb);
84    |  block_count      = bytestream2_get_be32(&gb);
    11←Calling 'bytestream2_get_be32'→
85    |     bytestream2_skip(&gb, 4);
86    |     chunk = bytestream2_get_le32(&gb);
87    |  if (chunk == MKTAG('c', 'o', 'm', 'p')) {
88    |         block_size = bytestream2_get_be16(&gb);
89    |     } else if (chunk == MKTAG('d', 'e', 'c', 0)) {
90    |         block_size = bytestream2_get_be16(&gb);
91    |     } else {
92    |  return AVERROR_INVALIDDATA;
93    |     }
94    |  
95    |  if (block_size < 8)
96    |  return AVERROR_INVALIDDATA;
97    |     par->block_align = block_size;
98    |     st->duration = 1024 * block_count;
99    |  
100   |     avio_seek(pb, s->internal->data_offset, SEEK_SET);
101   |     avpriv_set_pts_info(st, 64, 1, par->sample_rate);
102   |  
103   |  return 0;
104   | }
105   |  
106   | static int hca_read_packet(AVFormatContext *s, AVPacket *pkt)
107   | {
108   |     AVCodecParameters *par = s->streams[0]->codecpar;
109   |  int ret;
110   |  
111   |     ret = av_get_packet(s->pb, pkt, par->block_align);
112   |     pkt->duration = 1024;
113   |  return ret;
114   | }
42    | #define DEF(type, name, bytes, read, write)                                  \
43    | static av_always_inline type bytestream_get_ ## name(const uint8_t **b)        \
44    | {                                                                              \
45    |  (*b) += bytes;                                                             \
46    |  return read(*b - bytes);                                                   \
47    | }                                                                              \
48    | static av_always_inline void bytestream_put_ ## name(uint8_t **b,              \
49    |  const type value)         \
50    | {                                                                              \
51    |  write(*b, value);                                                          \
52    |  (*b) += bytes;                                                             \
53    | }                                                                              \
54    | static av_always_inline void bytestream2_put_ ## name ## u(PutByteContext *p,  \
55    |  const type value)   \
56    | {                                                                              \
57    |  bytestream_put_ ## name(&p->buffer, value);                                \
58    | }                                                                              \
59    | static av_always_inline void bytestream2_put_ ## name(PutByteContext *p,       \
60    |  const type value)        \
61    | {                                                                              \
62    |  if (!p->eof && (p->buffer_end - p->buffer >= bytes)) {                     \
63    |  write(p->buffer, value);                                               \
64    |  p->buffer += bytes;                                                    \
65    |  } else                                                                     \
66    |  p->eof = 1;                                                            \
67    | }                                                                              \
68    | static av_always_inline type bytestream2_get_ ## name ## u(GetByteContext *g)  \
69    | {                                                                              \
70    |  return bytestream_get_ ## name(&g->buffer);                                \
71    | }                                                                              \
72    | static av_always_inline type bytestream2_get_ ## name(GetByteContext *g)       \
73    | {                                                                              \
74    |  if (g->buffer_end - g->buffer < bytes) {                                   \
75    |  g->buffer = g->buffer_end;                                             \
76    |  return 0;                                                              \
77    |  }                                                                          \
78    |  return bytestream2_get_ ## name ## u(g);                                   \
79    | }                                                                              \
80    | static av_always_inline type bytestream2_peek_ ## name ## u(GetByteContext *g) \
81    | {                                                                              \
82    |  return read(g->buffer);                                                    \
83    | }                                                                              \
84    | static av_always_inline type bytestream2_peek_ ## name(GetByteContext *g)      \
85    | {                                                                              \
86    |  if (g->buffer_end - g->buffer < bytes)                                     \
87    |  return 0;                                                              \
88    |  return bytestream2_peek_ ## name ## u(g);                                  \
89    | }
90    |  
91    | DEF(uint64_t,     le64, 8, AV_RL64, AV_WL64)
92    | DEF(unsigned int, le32, 4, AV_RL32, AV_WL32)
93    | DEF(unsigned int, le24, 3, AV_RL24, AV_WL24)
94    | DEF(unsigned int, le16, 2, AV_RL16, AV_WL16)
95    | DEF(uint64_t,     be64, 8, AV_RB64, AV_WB64)
96    | DEF(unsigned int, be32, 4, AV_RB32, AV_WB32)
    12←Assuming the condition is false→
    13←Taking false branch→
    14←Calling 'bytestream2_get_be32u'→
    15←Calling 'bytestream_get_be32'→
    16←buffer read by avio_read may be partially uninitialized
97    | DEF(unsigned int, be24, 3, AV_RB24, AV_WB24)
98    | DEF(unsigned int, be16, 2, AV_RB16, AV_WB16)
99    | DEF(unsigned int, byte, 1, AV_RB8 , AV_WB8)
100   |  
101   | #if AV_HAVE_BIGENDIAN
102   | #   define bytestream2_get_ne16  bytestream2_get_be16
103   | #   define bytestream2_get_ne24  bytestream2_get_be24
104   | #   define bytestream2_get_ne32  bytestream2_get_be32
105   | #   define bytestream2_get_ne64  bytestream2_get_be64
106   | #   define bytestream2_get_ne16u bytestream2_get_be16u
107   | #   define bytestream2_get_ne24u bytestream2_get_be24u
108   | #   define bytestream2_get_ne32u bytestream2_get_be32u
109   | #   define bytestream2_get_ne64u bytestream2_get_be64u
110   | #   define bytestream2_put_ne16  bytestream2_put_be16
111   | #   define bytestream2_put_ne24  bytestream2_put_be24
112   | #   define bytestream2_put_ne32  bytestream2_put_be32
113   | #   define bytestream2_put_ne64  bytestream2_put_be64
114   | #   define bytestream2_peek_ne16 bytestream2_peek_be16
115   | #   define bytestream2_peek_ne24 bytestream2_peek_be24
116   | #   define bytestream2_peek_ne32 bytestream2_peek_be32
117   | #   define bytestream2_peek_ne64 bytestream2_peek_be64
118   | #else
119   | #   define bytestream2_get_ne16  bytestream2_get_le16
120   | #   define bytestream2_get_ne24  bytestream2_get_le24
121   | #   define bytestream2_get_ne32  bytestream2_get_le32
122   | #   define bytestream2_get_ne64  bytestream2_get_le64
123   | #   define bytestream2_get_ne16u bytestream2_get_le16u
124   | #   define bytestream2_get_ne24u bytestream2_get_le24u
125   | #   define bytestream2_get_ne32u bytestream2_get_le32u
126   | #   define bytestream2_get_ne64u bytestream2_get_le64u
127   | #   define bytestream2_put_ne16  bytestream2_put_le16
128   | #   define bytestream2_put_ne24  bytestream2_put_le24
129   | #   define bytestream2_put_ne32  bytestream2_put_le32
130   | #   define bytestream2_put_ne64  bytestream2_put_le64
131   | #   define bytestream2_peek_ne16 bytestream2_peek_le16
132   | #   define bytestream2_peek_ne24 bytestream2_peek_le24
133   | #   define bytestream2_peek_ne32 bytestream2_peek_le32
134   | #   define bytestream2_peek_ne64 bytestream2_peek_le64
135   | #endif
136   |  
137   | static av_always_inline void bytestream2_init(GetByteContext *g,
138   |  const uint8_t *buf,
139   |  int buf_size)
140   | {
141   |  av_assert0(buf_size >= 0);
142   |     g->buffer       = buf;
143   |     g->buffer_start = buf;
144   |     g->buffer_end   = buf + buf_size;
145   | }
146   |  
147   | static av_always_inline void bytestream2_init_writer(PutByteContext *p,
148   |                                                      uint8_t *buf,
149   |  int buf_size)
150   | {
151   |  av_assert0(buf_size >= 0);
152   |     p->buffer       = buf;
153   |     p->buffer_start = buf;
154   |     p->buffer_end   = buf + buf_size;
155   |     p->eof          = 0;
156   | }
157   |  
158   | static av_always_inline int bytestream2_get_bytes_left(GetByteContext *g)
159   | {
160   |  return g->buffer_end - g->buffer;
161   | }
162   |  
163   | static av_always_inline int bytestream2_get_bytes_left_p(PutByteContext *p)
164   | {
165   |  return p->buffer_end - p->buffer;
166   | }
167   |  
168   | static av_always_inline void bytestream2_skip(GetByteContext *g,
169   |  unsigned int size)
170   | {
171   |     g->buffer += FFMIN(g->buffer_end - g->buffer, size);
172   | }
173   |  
174   | static av_always_inline void bytestream2_skipu(GetByteContext *g,