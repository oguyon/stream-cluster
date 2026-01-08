import unittest
import os
from image_cluster import ImageCluster

class TestImageCluster(unittest.TestCase):
    def test_sequence(self):
        # Create 2 clusters: around (0,0) and (10,10)
        data = [
            [0.0, 0.0], [0.1, 0.0], [0.0, 0.1], # Cluster 0
            [10.0, 10.0], [10.1, 10.0], [10.0, 10.1] # Cluster 1
        ]

        # rlim 0.5 should separate them
        ic = ImageCluster(rlim=0.5, binary_path="../build/image-cluster", maxcl=5)
        res = ic.run_sequence(data)

        print("Stdout:", res.get('stdout'))

        self.assertEqual(res['total_clusters'], 2)
        self.assertEqual(len(res['assignments']), 6)
        # First 3 should be same cluster
        self.assertEqual(res['assignments'][0], res['assignments'][1])
        self.assertEqual(res['assignments'][0], res['assignments'][2])
        # Last 3 should be same cluster
        self.assertEqual(res['assignments'][3], res['assignments'][4])
        self.assertEqual(res['assignments'][3], res['assignments'][5])
        # Clusters should be different
        self.assertNotEqual(res['assignments'][0], res['assignments'][3])

if __name__ == '__main__':
    unittest.main()
