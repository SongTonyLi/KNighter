### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/./libavcodec/flac.h  
---|---  
Warning:| line 66, column 15  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


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
22    | #include "libavutil/channel_layout.h"
23    | #include "libavutil/mem.h"
24    | #include "libavcodec/avcodec.h"
25    | #include "libavcodec/bytestream.h"
26    | #include "libavcodec/flac.h"
27    | #include "avformat.h"
28    | #include "avio_internal.h"
29    | #include "demux.h"
30    | #include "flac_picture.h"
31    | #include "internal.h"
32    | #include "rawdec.h"
33    | #include "oggdec.h"
34    | #include "replaygain.h"
35    |  
36    | #define SEEKPOINT_SIZE 18
37    |  
38    | typedef struct FLACDecContext {
39    |     FFRawDemuxerContext rawctx;
40    |  int found_seektable;
41    |  
42    |     AVCodecContext *parser_dec;
43    | } FLACDecContext;
44    |  
45    | static void reset_index_position(int64_t metadata_head_size, AVStream *st)
46    | {
47    |     FFStream *const sti = ffstream(st);
48    |  /* the real seek index offset should be the size of metadata blocks with the offset in the frame blocks */
49    |  for (int i = 0; i < sti->nb_index_entries; i++)
50    |         sti->index_entries[i].pos += metadata_head_size;
51    | }
52    |  
53    | static const uint16_t sr_table[16] = {
54    |     0, 1764, 3528, 3840, 160, 320, 441, 480, 640, 882, 960, 1920, 0, 0, 0, 0
55    | };
56    |  
57    | static int flac_read_header(AVFormatContext *s)
58    | {
59    |  int ret, metadata_last=0, metadata_type, metadata_size, found_streaminfo=0;
60    |     uint8_t header[4];
61    |     uint8_t *buffer=NULL;
62    |     uint32_t marker;
63    |     FLACDecContext *flac = s->priv_data;
64    |     AVStream *st = avformat_new_stream(s, NULL);
65    |  if (!st)
    1Assuming 'st' is non-null→
    2←Taking false branch→
66    |  return AVERROR(ENOMEM);
67    |  st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
68    |     st->codecpar->codec_id = AV_CODEC_ID_FLAC;
69    |     ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL_RAW;
70    |  /* the parameters will be extracted from the compressed bitstream */
71    |  
72    |  /* if fLaC marker is not found, assume there is no header */
73    |     marker = avio_rl32(s->pb);
74    |  if (marker != MKTAG('f','L','a','C')) {
    3←Assuming the condition is false→
75    |  const int sample_rate = 50 * sr_table[(marker >> 16) & 0xF];
76    |  if (sample_rate)
77    |             avpriv_set_pts_info(st, 64, 1, sample_rate);
78    |         avio_seek(s->pb, -4, SEEK_CUR);
79    |  return 0;
80    |     }
81    |  
82    |  /* process metadata blocks */
83    |  while (!avio_feof(s->pb) && !metadata_last) {
    4←Assuming the condition is true→
    5←Loop condition is true.  Entering loop body→
84    |  ret = avio_read(s->pb, header, 4);
85    |  if (ret < 0) {
    6←Assuming 'ret' is >= 0→
    7←Taking false branch→
86    |  return ret;
87    |         } else if (ret != 4) {
    8←Assuming 'ret' is equal to 4→
    9←Taking false branch→
88    |  return AVERROR_EOF;
89    |         }
90    |  
91    |  flac_parse_block_header(header, &metadata_last, &metadata_type,
    10←Calling 'flac_parse_block_header'→
92    |  &metadata_size);
93    |  switch (metadata_type) {
94    |  /* allocate and read metadata block for supported types */
95    |  case FLAC_METADATA_TYPE_STREAMINFO:
96    |  case FLAC_METADATA_TYPE_CUESHEET:
97    |  case FLAC_METADATA_TYPE_PICTURE:
98    |  case FLAC_METADATA_TYPE_VORBIS_COMMENT:
99    |  case FLAC_METADATA_TYPE_SEEKTABLE:
100   |             buffer = av_mallocz(metadata_size + AV_INPUT_BUFFER_PADDING_SIZE);
101   |  if (!buffer) {
102   |  return AVERROR(ENOMEM);
103   |             }
104   |             ret = ffio_read_size(s->pb, buffer, metadata_size);
105   |  if (ret < 0)
106   |  goto fail;
107   |  
108   |  break;
109   |  /* skip metadata block for unsupported types */
110   |  default:
111   |             ret = avio_skip(s->pb, metadata_size);
112   |  if (ret < 0)
113   |  return ret;
114   |         }
115   |  
116   |  if (metadata_type == FLAC_METADATA_TYPE_STREAMINFO) {
117   |             uint32_t samplerate;
118   |             uint64_t samples;
119   |  
120   |  /* STREAMINFO can only occur once */
121   |  if (found_streaminfo) {
122   |  RETURN_ERROR(AVERROR_INVALIDDATA);
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
24    |  * FLAC (Free Lossless Audio Codec) common stuff
25    |  */
26    |  
27    | #ifndef AVCODEC_FLAC_H
28    | #define AVCODEC_FLAC_H
29    |  
30    | #include "libavutil/intreadwrite.h"
31    |  
32    | #define FLAC_STREAMINFO_SIZE   34
33    | #define FLAC_MAX_CHANNELS       8
34    | #define FLAC_MIN_BLOCKSIZE     16
35    | #define FLAC_MAX_BLOCKSIZE  65535
36    | #define FLAC_MIN_FRAME_SIZE    10
37    |  
38    | enum {
39    |     FLAC_CHMODE_INDEPENDENT = 0,
40    |     FLAC_CHMODE_LEFT_SIDE   = 1,
41    |     FLAC_CHMODE_RIGHT_SIDE  = 2,
42    |     FLAC_CHMODE_MID_SIDE    = 3,
43    | };
44    |  
45    | enum {
46    |     FLAC_METADATA_TYPE_STREAMINFO = 0,
47    |     FLAC_METADATA_TYPE_PADDING,
48    |     FLAC_METADATA_TYPE_APPLICATION,
49    |     FLAC_METADATA_TYPE_SEEKTABLE,
50    |     FLAC_METADATA_TYPE_VORBIS_COMMENT,
51    |     FLAC_METADATA_TYPE_CUESHEET,
52    |     FLAC_METADATA_TYPE_PICTURE,
53    |     FLAC_METADATA_TYPE_INVALID = 127
54    | };
55    |  
56    | /**
57    |  * Parse the metadata block parameters from the header.
58    |  * @param[in]  block_header header data, at least 4 bytes
59    |  * @param[out] last indicator for last metadata block
60    |  * @param[out] type metadata block type
61    |  * @param[out] size metadata block size
62    |  */
63    | static av_always_inline void flac_parse_block_header(const uint8_t *block_header,
64    |  int *last, int *type, int *size)
65    | {
66    |  int tmp = *block_header;
    11←buffer read by avio_read may be partially uninitialized
67    |  if (last)
68    |         *last = tmp & 0x80;
69    |  if (type)
70    |         *type = tmp & 0x7F;
71    |  if (size)
72    |         *size = AV_RB24(block_header + 1);
73    | }
74    |  
75    | #endif /* AVCODEC_FLAC_H */