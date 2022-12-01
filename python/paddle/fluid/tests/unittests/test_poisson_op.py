#   Copyright (c) 2021 PaddlePaddle Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import math
import unittest

import numpy as np
from op_test import OpTest

import paddle
from paddle.fluid.framework import _test_eager_guard

paddle.enable_static()
paddle.seed(100)


def output_hist(out, lam, a, b):
    prob = []
    bin = []
    for i in range(a, b + 1):
        prob.append((lam**i) * math.exp(-lam) / math.factorial(i))
        bin.append(i)
    bin.append(b + 0.1)

    hist, _ = np.histogram(out, bin)
    hist = hist.astype("float32")
    hist = hist / float(out.size)
    return hist, prob


class TestPoissonOp1(OpTest):
    def setUp(self):
        self.op_type = "poisson"
        self.config()

        self.attrs = {}
        self.inputs = {'X': np.full([2048, 1024], self.lam, dtype=self.dtype)}
        self.outputs = {'Out': np.ones([2048, 1024], dtype=self.dtype)}

    def config(self):
        self.lam = 10
        self.a = 5
        self.b = 15
        self.dtype = "float64"

    def verify_output(self, outs):
        hist, prob = output_hist(np.array(outs[0]), self.lam, self.a, self.b)
        np.testing.assert_allclose(hist, prob, rtol=0.01)

    def test_check_output(self):
        self.check_output_customized(self.verify_output)

    def test_check_grad_normal(self):
        self.check_grad(
            ['X'],
            'Out',
            user_defined_grads=[np.zeros([2048, 1024], dtype=self.dtype)],
            user_defined_grad_outputs=[
                np.random.rand(2048, 1024).astype(self.dtype)
            ],
        )


class TestPoissonOp2(TestPoissonOp1):
    def config(self):
        self.lam = 5
        self.a = 1
        self.b = 8
        self.dtype = "float32"


class TestPoissonAPI(unittest.TestCase):
    def test_static(self):
        with paddle.static.program_guard(
            paddle.static.Program(), paddle.static.Program()
        ):
            x_np = np.random.rand(10, 10)
            x = paddle.static.data(name="x", shape=[10, 10], dtype='float64')
            y = paddle.poisson(x)

            exe = paddle.static.Executor()
            y_np = exe.run(
                paddle.static.default_main_program(),
                feed={"x": x_np},
                fetch_list=[y],
            )
            self.assertTrue(np.min(y_np) >= 0)

    def test_dygraph(self):
        with paddle.fluid.dygraph.base.guard():
            x = paddle.randn([10, 10], dtype='float32')
            y = paddle.poisson(x)
            self.assertTrue(np.min(y.numpy()) >= 0)

            with _test_eager_guard():
                x = paddle.randn([10, 10], dtype='float32')
                x.stop_gradient = False
                y = paddle.poisson(x)
                y.backward()
                self.assertTrue(np.min(y.numpy()) >= 0)
                np.testing.assert_array_equal(np.zeros_like(x), x.gradient())

    def test_fixed_random_number(self):
        # Test GPU Fixed random number, which is generated by 'curandStatePhilox4_32_10_t'
        if not paddle.is_compiled_with_cuda():
            return

        print("Test Fixed Random number on GPU------>")
        paddle.disable_static()
        paddle.set_device('gpu')
        paddle.seed(2021)
        x = paddle.full([32, 3, 1024, 768], 10.0, dtype="float32")
        y = paddle.poisson(x)
        y_np = y.numpy()

        expect = [
            13.0,
            13.0,
            11.0,
            8.0,
            12.0,
            6.0,
            9.0,
            15.0,
            16.0,
            6.0,
            13.0,
            12.0,
            9.0,
            15.0,
            17.0,
            8.0,
            11.0,
            16.0,
            11.0,
            10.0,
        ]
        np.testing.assert_array_equal(y_np[0, 0, 0, 0:20], expect)

        expect = [
            15.0,
            7.0,
            12.0,
            8.0,
            14.0,
            10.0,
            10.0,
            11.0,
            11.0,
            11.0,
            21.0,
            6.0,
            9.0,
            13.0,
            13.0,
            11.0,
            6.0,
            9.0,
            12.0,
            12.0,
        ]
        np.testing.assert_array_equal(y_np[8, 1, 300, 200:220], expect)

        expect = [
            10.0,
            15.0,
            9.0,
            6.0,
            4.0,
            13.0,
            10.0,
            10.0,
            13.0,
            12.0,
            9.0,
            7.0,
            10.0,
            14.0,
            7.0,
            10.0,
            8.0,
            5.0,
            10.0,
            14.0,
        ]
        np.testing.assert_array_equal(y_np[16, 1, 600, 400:420], expect)

        expect = [
            10.0,
            9.0,
            14.0,
            12.0,
            8.0,
            9.0,
            7.0,
            8.0,
            11.0,
            10.0,
            13.0,
            8.0,
            12.0,
            9.0,
            7.0,
            8.0,
            11.0,
            11.0,
            12.0,
            5.0,
        ]
        np.testing.assert_array_equal(y_np[24, 2, 900, 600:620], expect)

        expect = [
            15.0,
            5.0,
            11.0,
            13.0,
            12.0,
            12.0,
            13.0,
            16.0,
            9.0,
            9.0,
            7.0,
            9.0,
            13.0,
            11.0,
            15.0,
            6.0,
            11.0,
            9.0,
            10.0,
            10.0,
        ]
        np.testing.assert_array_equal(y_np[31, 2, 1023, 748:768], expect)

        x = paddle.full([16, 1024, 1024], 5.0, dtype="float32")
        y = paddle.poisson(x)
        y_np = y.numpy()
        expect = [
            4.0,
            5.0,
            2.0,
            9.0,
            8.0,
            7.0,
            4.0,
            7.0,
            4.0,
            7.0,
            6.0,
            3.0,
            10.0,
            7.0,
            5.0,
            7.0,
            2.0,
            5.0,
            5.0,
            6.0,
        ]
        np.testing.assert_array_equal(y_np[0, 0, 100:120], expect)

        expect = [
            1.0,
            4.0,
            8.0,
            11.0,
            6.0,
            5.0,
            4.0,
            4.0,
            7.0,
            4.0,
            4.0,
            7.0,
            11.0,
            6.0,
            5.0,
            3.0,
            4.0,
            6.0,
            3.0,
            3.0,
        ]
        np.testing.assert_array_equal(y_np[4, 300, 300:320], expect)

        expect = [
            7.0,
            5.0,
            4.0,
            6.0,
            8.0,
            5.0,
            6.0,
            7.0,
            7.0,
            7.0,
            3.0,
            10.0,
            5.0,
            10.0,
            4.0,
            5.0,
            8.0,
            7.0,
            5.0,
            7.0,
        ]
        np.testing.assert_array_equal(y_np[8, 600, 600:620], expect)

        expect = [
            8.0,
            6.0,
            7.0,
            4.0,
            3.0,
            0.0,
            4.0,
            6.0,
            6.0,
            4.0,
            3.0,
            10.0,
            5.0,
            1.0,
            3.0,
            8.0,
            8.0,
            2.0,
            1.0,
            4.0,
        ]
        np.testing.assert_array_equal(y_np[12, 900, 900:920], expect)

        expect = [
            2.0,
            1.0,
            14.0,
            3.0,
            6.0,
            5.0,
            2.0,
            2.0,
            6.0,
            5.0,
            7.0,
            4.0,
            8.0,
            4.0,
            8.0,
            4.0,
            5.0,
            7.0,
            1.0,
            7.0,
        ]
        np.testing.assert_array_equal(y_np[15, 1023, 1000:1020], expect)
        paddle.enable_static()


if __name__ == "__main__":
    unittest.main()
