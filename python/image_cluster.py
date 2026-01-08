import subprocess
import os
import tempfile
import shutil
import re

class ImageCluster:
    def __init__(self, rlim, binary_path="./build/image-cluster", **kwargs):
        self.rlim = rlim
        self.binary_path = binary_path
        self.options = kwargs

    def run(self, input_file, output_dir=None):
        """
        Run clustering on an input file.

        Args:
            input_file (str): Path to input file.
            output_dir (str, optional): Output directory.

        Returns:
            dict: Results containing 'stdout', 'stats', and parsed 'assignments' if available.
        """
        cmd = [self.binary_path, str(self.rlim)]

        # Add options
        for k, v in self.options.items():
            if v is True:
                cmd.append(f"-{k}")
            elif v is not False and v is not None:
                cmd.append(f"-{k}")
                cmd.append(str(v))

        if output_dir:
            cmd.append("-outdir")
            cmd.append(output_dir)

        cmd.append(input_file)

        # print(f"Running: {' '.join(cmd)}")
        result = subprocess.run(cmd, capture_output=True, text=True)

        if result.returncode != 0:
            raise RuntimeError(f"image-cluster failed:\n{result.stderr}")

        res = self._parse_stdout(result.stdout)

        # Locate output file to parse assignments
        # The C binary writes clustered.txt adjacent to the input file, regardless of -outdir
        clustered_path = None
        if input_file.endswith('.txt'):
            clustered_path = input_file.replace('.txt', '.clustered.txt')
        else:
            clustered_path = input_file + '.clustered.txt'

        if clustered_path and os.path.exists(clustered_path):
            res.update(self._read_clustered_file(clustered_path))

        return res

    def run_sequence(self, data):
        """
        Run clustering on a sequence of points (list of lists or numpy array).

        Args:
            data: List of coordinates [ [x,y,...], ... ]

        Returns:
            dict: Clustering results including 'assignments'.
        """
        # Create temp file
        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as tmp:
            tmp_path = tmp.name
            for point in data:
                line = " ".join(map(str, point))
                tmp.write(line + "\n")

        out_dir = tempfile.mkdtemp()
        try:
            return self.run(tmp_path, output_dir=out_dir)
        finally:
            if os.path.exists(tmp_path):
                # Also remove the generated clustered file
                clustered = tmp_path.replace('.txt', '.clustered.txt')
                if os.path.exists(clustered):
                    os.remove(clustered)
                os.remove(tmp_path)
            if os.path.exists(out_dir):
                shutil.rmtree(out_dir)

    def _parse_stdout(self, stdout):
        res = {'stdout': stdout}
        for line in stdout.split('\n'):
            if "Total clusters:" in line:
                res['total_clusters'] = int(line.split(':')[1].strip())
            if "Processing time:" in line:
                try:
                    res['time_ms'] = float(line.split(':')[1].strip().replace('ms',''))
                except:
                    pass
        return res

    def _read_clustered_file(self, filepath):
        assignments = []
        # Clusters dict: id -> list of frames (or just use assignments)

        with open(filepath, 'r') as f:
            for line in f:
                if line.startswith('#'): continue
                parts = line.strip().split()
                if len(parts) < 2: continue
                # FrameIndex ClusterID Data...
                try:
                    # fid = int(parts[0])
                    cid = int(parts[1])
                    assignments.append(cid)
                except ValueError:
                    continue
        return {'assignments': assignments}
