import re
import subprocess as sp
from pathlib import Path
from typing import List

from loguru import logger

from targets.factory import TargetFactory


class FFmpeg(TargetFactory):
    """
    A class representing the FFmpeg repository.
    """

    _target_type = "ffmpeg"
    _build_commands = None  # FFmpeg object paths are derived directly; no command DB needed

    def checkout_commit(self, commit_id, is_before=False, **kwargs):
        """
        Checkout a specific commit in the FFmpeg repository and configure the build.

        Args:
            commit_id (str): The commit ID to checkout.
            is_before (bool): Whether to checkout before the commit.
        """
        logger.info(
            f"Checking out commit {commit_id} {'before' if is_before else 'after'}"
        )

        # Reset any uncommitted changes
        try:
            self.repo.git.reset("--hard")
            self.repo.git.clean("-fdx")
        except Exception as e:
            logger.warning(f"Failed to clean repository state: {e}")

        if is_before:
            commit_id = commit_id + "^"

        self.repo.git.checkout(commit_id)

        # Configure FFmpeg with clang and debug symbols, optimizations disabled
        # so CSA can analyze the code more effectively.
        configure_cmd = [
            "./configure",
            "--disable-optimizations",
            "--enable-debug",
            "--cc=clang",
            "--cxx=clang++",
            "--disable-stripping",
            "--disable-doc",
        ]

        # Allow callers to pass extra configure flags (e.g. for cross-compilation)
        extra_flags = kwargs.get("configure_flags", [])
        configure_cmd.extend(extra_flags)

        logger.info(f"Configuring FFmpeg: {' '.join(configure_cmd)}")
        res = sp.run(
            configure_cmd,
            cwd=self.repo.working_dir,
            capture_output=True,
        )
        if res.returncode != 0:
            logger.error(f"FFmpeg configure failed:\n{res.stderr.decode()}")
            raise RuntimeError(f"FFmpeg configure failed: {res.stderr.decode()}")

        logger.info("FFmpeg configured successfully")

    @staticmethod
    def get_object_name(file_name: str) -> str:
        """
        Map a FFmpeg source file to its compiled object file.
        FFmpeg preserves the source directory structure, so the mapping is trivial.
        """
        return str(Path(file_name).with_suffix(".o"))

    @staticmethod
    def get_objects_from_patch(patch: str) -> List[str]:
        """
        Get the object files to analyze from a patch.
        """
        pattern = r"^--- a/(.*)$"
        matches = re.findall(pattern, patch, re.MULTILINE)
        # Only C source files; FFmpeg is almost entirely C
        matches = [m for m in matches if m.endswith(".c")]
        matches = [FFmpeg.get_object_name(m) for m in matches]
        return matches
