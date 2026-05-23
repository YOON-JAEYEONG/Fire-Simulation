from __future__ import annotations

import argparse
import pickle
from pathlib import Path

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper
from skl2onnx import convert_sklearn
from skl2onnx.common.data_types import FloatTensorType

try:
    from .schema import ACTION_ID_TO_NAME, OBSERVATION_DIM
except ImportError:
    from schema import ACTION_ID_TO_NAME, OBSERVATION_DIM

NUM_ACTIONS = len(ACTION_ID_TO_NAME)


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Export a trained YUFS Random Forest checkpoint to ONNX for Unreal NNE/ORT inference.")
    parser.add_argument("--checkpoint", required=True, help="Path to rf_model.pkl produced by train_rf.py.")
    parser.add_argument("--output", required=True, help="Path to the exported ONNX file.")
    parser.add_argument("--opset", type=int, default=17, help="ONNX opset version.")
    return parser


def append_class_expansion(onnx_model: onnx.ModelProto, classes: np.ndarray) -> onnx.ModelProto:
    """RF가 학습한 클래스(예: 9개)의 probabilities를 전체 액션 공간(12개)으로 확장.

    skl2onnx RF의 probabilities 출력은 학습 시 등장한 클래스 수만큼만 나온다.
    YUFSRLPolicy는 index를 직접 EYUFSAction enum 값으로 사용하므로,
    [batch, n_seen] → [batch, NUM_ACTIONS] 확장이 필수.

    구현: probabilities @ P (MatMul)
    P[i, classes[i]] = 1.0 인 [n_seen, NUM_ACTIONS] 치환 행렬.
    """
    n_seen = len(classes)
    if n_seen == NUM_ACTIONS:
        return onnx_model  # 모든 클래스 존재 → 확장 불필요

    # 치환 행렬 P: [n_seen, NUM_ACTIONS]
    P = np.zeros((n_seen, NUM_ACTIONS), dtype=np.float32)
    for i, cls_id in enumerate(classes):
        P[i, int(cls_id)] = 1.0

    graph = onnx_model.graph

    # label 출력 제거, probabilities만 남기기
    while len(graph.output) > 1:
        del graph.output[0]

    prob_output_name = graph.output[0].name  # e.g. "probabilities"

    # P를 initializer로 추가
    p_initializer = numpy_helper.from_array(P, name="class_expansion_matrix")
    graph.initializer.append(p_initializer)

    # MatMul 노드 추가: probabilities @ P → expanded_probs
    matmul_node = helper.make_node(
        "MatMul",
        inputs=[prob_output_name, "class_expansion_matrix"],
        outputs=["action_logits"],
    )
    graph.node.append(matmul_node)

    # 그래프 출력을 expanded_probs로 교체
    del graph.output[0]
    graph.output.append(
        helper.make_tensor_value_info("action_logits", TensorProto.FLOAT, [None, NUM_ACTIONS])
    )

    return onnx_model


def main() -> int:
    args = build_argument_parser().parse_args()
    checkpoint_path = Path(args.checkpoint)
    output_path = Path(args.output)

    if not checkpoint_path.is_file():
        raise SystemExit(f"Checkpoint not found: {checkpoint_path}")

    with checkpoint_path.open("rb") as f:
        clf = pickle.load(f)

    classes = clf.classes_
    n_seen = len(classes)
    missing = [ACTION_ID_TO_NAME[i] for i in range(NUM_ACTIONS) if i not in classes]
    print(f"rf_classes={n_seen}/{NUM_ACTIONS}  missing={missing if missing else 'none'}")

    initial_type = [("observation", FloatTensorType([None, OBSERVATION_DIM]))]
    onnx_model = convert_sklearn(
        clf,
        initial_types=initial_type,
        target_opset=args.opset,
        options={type(clf): {"zipmap": False}},
    )

    onnx_model = append_class_expansion(onnx_model, classes)
    onnx.checker.check_model(onnx_model)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("wb") as f:
        f.write(onnx_model.SerializeToString())

    print(f"output_shape=[batch, {NUM_ACTIONS}]")
    print(f"saved_onnx={output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
