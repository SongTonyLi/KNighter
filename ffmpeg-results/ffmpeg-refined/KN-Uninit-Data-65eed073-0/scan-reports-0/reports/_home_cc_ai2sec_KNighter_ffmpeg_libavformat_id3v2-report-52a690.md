### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/id3v2.c  
---|---  
Warning:| line 1115, column 55  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


93    |     { 0 },
94    | };
95    |  
96    | const char ff_id3v2_4_tags[][4] = {
97    |  "TDEN", "TDOR", "TDRC", "TDRL", "TDTG", "TIPL", "TMCL", "TMOO",
98    |  "TPRO", "TSOA", "TSOP", "TSOT", "TSST",
99    |     { 0 },
100   | };
101   |  
102   | const char ff_id3v2_3_tags[][4] = {
103   |  "TDAT", "TIME", "TORY", "TRDA", "TSIZ", "TYER",
104   |     { 0 },
105   | };
106   |  
107   | const char * const ff_id3v2_picture_types[21] = {
108   |  "Other",
109   |  "32x32 pixels 'file icon'",
110   |  "Other file icon",
111   |  "Cover (front)",
112   |  "Cover (back)",
113   |  "Leaflet page",
114   |  "Media (e.g. label side of CD)",
115   |  "Lead artist/lead performer/soloist",
116   |  "Artist/performer",
117   |  "Conductor",
118   |  "Band/Orchestra",
119   |  "Composer",
120   |  "Lyricist/text writer",
121   |  "Recording Location",
122   |  "During recording",
123   |  "During performance",
124   |  "Movie/video screen capture",
125   |  "A bright coloured fish",
126   |  "Illustration",
127   |  "Band/artist logotype",
128   |  "Publisher/Studio logotype",
129   | };
130   |  
131   | const CodecMime ff_id3v2_mime_tags[] = {
132   |     { "image/gif",  AV_CODEC_ID_GIF   },
133   |     { "image/jpeg", AV_CODEC_ID_MJPEG },
134   |     { "image/jpg",  AV_CODEC_ID_MJPEG },
135   |     { "image/png",  AV_CODEC_ID_PNG   },
136   |     { "image/tiff", AV_CODEC_ID_TIFF  },
137   |     { "image/bmp",  AV_CODEC_ID_BMP   },
138   |     { "JPG",        AV_CODEC_ID_MJPEG }, /* ID3v2.2  */
139   |     { "PNG",        AV_CODEC_ID_PNG   }, /* ID3v2.2  */
140   |     { "",           AV_CODEC_ID_NONE  },
141   | };
142   |  
143   | int ff_id3v2_match(const uint8_t *buf, const char *magic)
144   | {
145   |  return  buf[0]         == magic[0] &&
146   |             buf[1]         == magic[1] &&
147   |             buf[2]         == magic[2] &&
148   |             buf[3]         != 0xff     &&
149   |             buf[4]         != 0xff     &&
150   |            (buf[6] & 0x80) == 0        &&
151   |            (buf[7] & 0x80) == 0        &&
152   |            (buf[8] & 0x80) == 0        &&
153   |            (buf[9] & 0x80) == 0;
154   | }
155   |  
156   | int ff_id3v2_tag_len(const uint8_t *buf)
157   | {
158   |  int len = ((buf[6] & 0x7f) << 21) +
159   |               ((buf[7] & 0x7f) << 14) +
160   |               ((buf[8] & 0x7f) << 7) +
161   |               (buf[9] & 0x7f) +
162   |  ID3v2_HEADER_SIZE;
163   |  if (buf[5] & 0x10)
164   |         len += ID3v2_HEADER_SIZE;
165   |  return len;
166   | }
167   |  
168   | static unsigned int get_size(AVIOContext *s, int len)
169   | {
170   |  int v = 0;
171   |  while (len--)
172   |         v = (v << 7) + (avio_r8(s) & 0x7F);
173   |  return v;
174   | }
175   |  
176   | static unsigned int size_to_syncsafe(unsigned int size)
177   | {
178   |  return (((size) & (0x7f <<  0)) >> 0) +
179   |            (((size) & (0x7f <<  8)) >> 1) +
180   |            (((size) & (0x7f << 16)) >> 2) +
181   |            (((size) & (0x7f << 24)) >> 3);
182   | }
183   |  
1026  |  goto seek;
1027  |                         }
1028  |                         tlen = err;
1029  |                     }
1030  |  
1031  |                     err = uncompress(uncompressed_buffer, &dlen, buffer, tlen);
1032  |  if (err != Z_OK) {
1033  |                         av_log(s, AV_LOG_ERROR, "Failed to uncompress tag: %d\n", err);
1034  |  goto seek;
1035  |                     }
1036  |                     ffio_init_context(&pb_local, uncompressed_buffer, dlen, 0, NULL, NULL, NULL, NULL);
1037  |                     tlen = dlen;
1038  |                     pbx = &pb_local; // read from sync buffer
1039  |                 }
1040  | #endif
1041  |  if (tag[0] == 'T')
1042  |  /* parse text tag */
1043  |                 read_ttag(s, pbx, tlen, metadata, tag);
1044  |  else if (!memcmp(tag, "USLT", 4))
1045  |                 read_uslt(s, pbx, tlen, metadata);
1046  |  else if (!strcmp(tag, comm_frame))
1047  |                 read_comment(s, pbx, tlen, metadata);
1048  |  else
1049  |  /* parse special meta tag */
1050  |                 extra_func->read(s, pbx, tlen, tag, extra_meta, isv34);
1051  |         } else if (!tag[0]) {
1052  |  if (tag[1])
1053  |                 av_log(s, AV_LOG_WARNING, "invalid frame id, assuming padding\n");
1054  |             avio_skip(pb, tlen);
1055  |  break;
1056  |         }
1057  |  /* Skip to end of tag */
1058  | seek:
1059  |         avio_seek(pb, next, SEEK_SET);
1060  |     }
1061  |  
1062  |  /* Footer preset, always 10 bytes, skip over it */
1063  |  if (version == 4 && flags & 0x10)
1064  |         end += 10;
1065  |  
1066  | error:
1067  |  if (reason)
1068  |         av_log(s, AV_LOG_INFO, "ID3v2.%d tag skipped, cannot handle %s\n",
1069  |                version, reason);
1070  |     avio_seek(pb, end, SEEK_SET);
1071  |     av_free(buffer);
1072  |     av_free(uncompressed_buffer);
1073  |  return;
1074  | }
1075  |  
1076  | static void id3v2_read_internal(AVIOContext *pb, AVDictionary **metadata,
1077  |                                 AVFormatContext *s, const char *magic,
1078  |                                 ID3v2ExtraMeta **extra_metap, int64_t max_search_size)
1079  | {
1080  |  int len, ret;
1081  |     uint8_t buf[ID3v2_HEADER_SIZE];
1082  |     ExtraMetaList extra_meta = { NULL };
1083  |  int found_header;
1084  |     int64_t start, off;
1085  |  
1086  |  if (extra_metap)
    2←Assuming 'extra_metap' is null→
1087  |         *extra_metap = NULL;
1088  |  
1089  |  if (max_search_size && max_search_size < ID3v2_HEADER_SIZE)
    3←Assuming 'max_search_size' is 0→
1090  |  return;
1091  |  
1092  |  start = avio_tell(pb);
1093  |  do {
1094  |  /* save the current offset in case there's nothing to read/skip */
1095  |  off = avio_tell(pb);
1096  |  if (max_search_size3.1'max_search_size' is 0 && off - start >= max_search_size - ID3v2_HEADER_SIZE) {
1097  |             avio_seek(pb, off, SEEK_SET);
1098  |  break;
1099  |         }
1100  |  
1101  |  ret = ffio_ensure_seekback(pb, ID3v2_HEADER_SIZE);
1102  |  if (ret >= 0)
    4←Assuming 'ret' is >= 0→
    5←Taking true branch→
1103  |  ret = avio_read(pb, buf, ID3v2_HEADER_SIZE);
1104  |  if (ret != ID3v2_HEADER_SIZE) {
    6←Assuming 'ret' is equal to ID3v2_HEADER_SIZE→
    7←Taking false branch→
1105  |             avio_seek(pb, off, SEEK_SET);
1106  |  break;
1107  |         }
1108  |  found_header = ff_id3v2_match(buf, magic);
1109  |  if (found_header7.1'found_header' is 1) {
    8←Taking true branch→
1110  |  /* parse ID3v2 header */
1111  |  len = ((buf[6] & 0x7f) << 21) |
1112  |                   ((buf[7] & 0x7f) << 14) |
1113  |                   ((buf[8] & 0x7f) << 7) |
1114  |                    (buf[9] & 0x7f);
1115  |  id3v2_parse(pb, metadata, s, len, buf[3], buf[5],
    9←buffer read by avio_read may be partially uninitialized
1116  |                         extra_metap ? &extra_meta : NULL);
1117  |         } else {
1118  |             avio_seek(pb, off, SEEK_SET);
1119  |         }
1120  |     } while (found_header);
1121  |     ff_metadata_conv(metadata, NULL, ff_id3v2_34_metadata_conv);
1122  |     ff_metadata_conv(metadata, NULL, id3v2_2_metadata_conv);
1123  |     ff_metadata_conv(metadata, NULL, ff_id3v2_4_metadata_conv);
1124  |     merge_date(metadata);
1125  |  if (extra_metap)
1126  |         *extra_metap = extra_meta.head;
1127  | }
1128  |  
1129  | void ff_id3v2_read_dict(AVIOContext *pb, AVDictionary **metadata,
1130  |  const char *magic, ID3v2ExtraMeta **extra_meta)
1131  | {
1132  |     id3v2_read_internal(pb, metadata, NULL, magic, extra_meta, 0);
1133  | }
1134  |  
1135  | void ff_id3v2_read(AVFormatContext *s, const char *magic,
1136  |                    ID3v2ExtraMeta **extra_meta, unsigned int max_search_size)
1137  | {
1138  |  id3v2_read_internal(s->pb, &s->metadata, s, magic, extra_meta, max_search_size);
    1Calling 'id3v2_read_internal'→
1139  | }
1140  |  
1141  | void ff_id3v2_free_extra_meta(ID3v2ExtraMeta **extra_meta)
1142  | {
1143  |     ID3v2ExtraMeta *current = *extra_meta, *next;
1144  |  const ID3v2EMFunc *extra_func;
1145  |  
1146  |  while (current) {
1147  |  if ((extra_func = get_extra_meta_func(current->tag, 1)))
1148  |             extra_func->free(¤t->data);
1149  |         next = current->next;
1150  |         av_freep(¤t);
1151  |         current = next;
1152  |     }
1153  |  
1154  |     *extra_meta = NULL;
1155  | }
1156  |  
1157  | int ff_id3v2_parse_apic(AVFormatContext *s, ID3v2ExtraMeta *extra_meta)
1158  | {
1159  |     ID3v2ExtraMeta *cur;
1160  |  
1161  |  for (cur = extra_meta; cur; cur = cur->next) {
1162  |         ID3v2ExtraMetaAPIC *apic;
1163  |         AVStream *st;
1164  |  int ret;
1165  |  
1166  |  if (strcmp(cur->tag, "APIC"))
1167  |  continue;
1168  |         apic = &cur->data.apic;