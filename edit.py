import subprocess
import os

def run_ffmpeg(cmd):
    print("Running:", " ".join(cmd))
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        print(result.stderr.decode())
    return result

def cut_first_minute(input_file="out.mp4", output_file="one.mp4", cut_time='00:01:00'):
    run_ffmpeg([
        "ffmpeg", "-y",
        "-i", input_file,
        "-t", "00:00:59",
        "-c", "copy",
        output_file
    ])

def fade_in(input_file="out.mp4", output_file="fade_in.mp4", fade_duration=3):
    import os

    temp_main = "clip_no_fade.mp4"
    temp_fade = "fade_part.mp4"

    # Step 1: Create the fading part (first N seconds with fade-in)
    run_ffmpeg([
        "ffmpeg", "-y",
        "-i", input_file,
        "-t", str(fade_duration),
        "-vf", f"fade=t=in:st=0:d={fade_duration}",
        "-af", f"afade=t=in:st=0:d={fade_duration}",
        temp_fade
    ])

    # Step 2: Get the rest of the video, skipping the fade_duration
    run_ffmpeg([
        "ffmpeg", "-y",
        "-ss", str(fade_duration),
        "-i", input_file,
        "-c", "copy",
        temp_main
    ])

    # Create concat list file
    with open("concat_list.txt", "w") as f:
        f.write(f"file '{temp_fade}'\n")
        f.write(f"file '{temp_main}'\n")

    # Step 3: Concatenate the two clips (video & audio safe method)
    run_ffmpeg([
        "ffmpeg", "-y",
        "-f", "concat",
        "-safe", "0",
        "-i", "concat_list.txt",
        "-c", "copy",
        output_file
    ])


    os.remove(temp_fade)
    os.remove(temp_main)
    os.remove("concat_list.txt")

def fade_out(input_file="out.mp4", output_file="fade_out.mp4", fade_duration=3):
    import os

    temp_main = "clip_before_fade.mp4"
    temp_fade = "fade_part.mp4"

    # Step 1: Get total duration of the video
    result = subprocess.run(
        ["ffprobe", "-v", "error", "-show_entries", "format=duration",
         "-of", "default=noprint_wrappers=1:nokey=1", input_file],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )
    total_duration = float(result.stdout.decode().strip())
    fade_start = total_duration - fade_duration

    # Step 2: Cut the fade-out part from the end
    run_ffmpeg([
        "ffmpeg", "-y",
        "-ss", str(fade_start),
        "-i", input_file,
        "-t", str(fade_duration),
        "-vf", f"fade=t=out:st=0:d={fade_duration}",
        "-af", f"afade=t=out:st=0:d={fade_duration}",
        temp_fade
    ])

    # Step 3: Cut the part before the fade
    run_ffmpeg([
        "ffmpeg", "-y",
        "-t", str(fade_start),
        "-i", input_file,
        "-c", "copy",
        temp_main
    ])

    # Step 4: Concatenate the two clips
    with open("concat_list.txt", "w") as f:
        f.write(f"file '{temp_main}'\n")
        f.write(f"file '{temp_fade}'\n")

    run_ffmpeg([
        "ffmpeg", "-y",
        "-f", "concat",
        "-safe", "0",
        "-i", "concat_list.txt",
        "-c", "copy",
        output_file
    ])

    # Step 5: Cleanup
    os.remove(temp_fade)
    os.remove(temp_main)
    os.remove("concat_list.txt")

def add_music_to_video(video_file="out.mp4", music_file="music.mp3", output_file="with_music.mp4", mix=True):
    import os

    temp_video_with_audio = "temp_with_silence.mp4"

    # Step 1: Add silent audio if needed
    run_ffmpeg([
        "ffmpeg", "-y",
        "-i", video_file,
        "-f", "lavfi", "-t", "9999",  # long enough dummy
        "-i", "anullsrc=channel_layout=stereo:sample_rate=48000",
        "-shortest",
        "-c:v", "copy", "-c:a", "aac",
        temp_video_with_audio
    ])

    # Step 2: Mix or Replace
    if mix:
        run_ffmpeg([
            "ffmpeg", "-y",
            "-i", temp_video_with_audio,
            "-i", music_file,
            "-filter_complex", "[0:a][1:a]amix=inputs=2:duration=first:dropout_transition=2",
            "-c:v", "copy",
            "-shortest",
            output_file
        ])
    else:
        run_ffmpeg([
            "ffmpeg", "-y",
            "-i", temp_video_with_audio,
            "-i", music_file,
            "-map", "0:v:0",
            "-map", "1:a:0",
            "-c:v", "copy",
            "-shortest",
            output_file
        ])

    os.remove(temp_video_with_audio)

def rotate_90(input_file="out.mp4", output_file="rotated.mp4", clockwise=True):
    transpose_value = "1" if clockwise else "2"  # 1 = 90° CW, 2 = 90° CCW
    run_ffmpeg([
        "ffmpeg", "-y",
        "-i", input_file,
        "-vf", f"transpose={transpose_value}",
        "-c:a", "copy",
        output_file
    ])

def make_iphone_compatible(input_file, output_file="iphone_ready.mp4"):
    run_ffmpeg([
        "ffmpeg", "-y",
        "-i", input_file,
        "-c:v", "libx264",
        "-profile:v", "baseline",
        "-level", "3.0",
        "-pix_fmt", "yuv420p",
        "-c:a", "aac",
        "-b:a", "128k",
        "-movflags", "+faststart",
        output_file
    ])

def scale_down_by_half(input_file="out.mp4", output_file="scaled_down.mp4"):
    run_ffmpeg([
        "ffmpeg", "-y",
        "-i", input_file,
        "-vf", "scale=iw/2:ih/2",
        "-c:a", "copy",
        output_file
    ])

def make_short(input_file='out.mp4', cut_time='00:00:59'):
    print(f"Cutting first: {cut_time}.")
    cut_first_minute(input_file, cut_time=cut_time)

    print("Rotating.")
    rotate_90(input_file='one.mp4')
    os.remove('one.mp4')

    print("Adding music.")
    add_music_to_video('rotated.mp4')
    os.remove('rotated.mp4')

    print("Scaling down by half.")
    scale_down_by_half('with_music.mp4')
    os.remove('with_music.mp4')

    print("Making Iphone Compatible")
    make_iphone_compatible('scaled_down.mp4')

    os.rename('iphone_ready.mp4', 'Short.mp4')

def make_long(input_file='out.mp4'):
    print("Fading in.")
    fade_in()

    print("Fading out.")
    fade_out('fade_in.mp4')
    os.remove('fade_in.mp4')

    print("Adding music.")
    add_music_to_video('fade_out.mp4')
    os.remove('fade_out.mp4')

    print("Making Iphone Compatible")
    os.remove('Long.mp4')

    os.rename('with_music.mp4', 'Long.mp4')


scale_down_by_half('Short.mp4', 'short small.mp4')
scale_down_by_half('Satisfying Colored Balls.mp4', 'large small.mp4')

# if __name__ == '__main__':
    # make_short()
    # make_long()
