set -ex

python3 main.py gen --config_file ../config-ffmpeg.yaml --commit_file=../commits/ffmpeg_commits.txt
python3 main.py refine --config_file ../config-ffmpeg-refine.yaml ../results/ffmpeg
python3 main.py triage --config_file ../config-ffmpeg-triage.yaml ../results/ffmpeg-refined
