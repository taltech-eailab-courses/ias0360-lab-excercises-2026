
import argparse
from pathlib import Path

import numpy as np
from scipy.signal import butter, sosfilt


COUNTS_PER_G = 16384.0
SAMPLING_RATE_HZ = 2000.0

# Fixed label order: integer label i corresponds to CLASS_NAMES[i].
CLASS_NAMES = ("negative", "ring", "slope", "wing")

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_DATASET_DIR = SCRIPT_DIR.parent / "dataset" / "custom"
DEFAULT_OUTPUT_PATH = DEFAULT_DATASET_DIR / "imu_dataset.npz"



def validate_xyz(data):
    """Validate an (N, 3) acceleration array."""
    data = np.asarray(data, dtype=np.float64)

    if data.ndim != 2 or data.shape[1] != 3:
        raise ValueError("Expected acceleration shape (N, 3).")
    if len(data) < 2:
        raise ValueError("At least two samples are required.")
    if not np.isfinite(data).all():
        raise ValueError("Data contains NaN or infinite values.")

    return data


def load_csv(path):
    """
    Load one recording:
        ax,ay,az
        12,102,16701
        ...

    Values must already be decoded as signed sensor counts.
    """
    data = np.loadtxt(
        Path(path),
        delimiter=",",
        skiprows=1,
        ndmin=2,
    )
    return validate_xyz(data)


def counts_to_g(raw_xyz, bias_counts=None):
    """
    Convert signed raw counts to g.

    bias_counts is sensor bias, not the full stationary reading.
    """
    raw_xyz = validate_xyz(raw_xyz)

    if np.any((raw_xyz < -32768) | (raw_xyz > 32767)):
        raise ValueError("Raw values exceed signed 16-bit range.")

    bias = np.zeros(3) if bias_counts is None else np.asarray(
        bias_counts, dtype=np.float64
    )
    if bias.shape != (3,) or not np.isfinite(bias).all():
        raise ValueError("bias_counts must contain three finite values.")

    return (raw_xyz - bias) / COUNTS_PER_G


def estimate_bias_six_positions(stationary_recordings):
    """
    Estimate per-axis bias from six stationary recordings.

    Required keys: x+, x-, y+, y-, z+, z-.
    Each key identifies the sensor axis aligned upward (+) or
    downward (-). Recordings contain raw counts.

    Midpoints cancel the approximately +1g and -1g readings.
    This estimates bias only, not scale or cross-axis calibration.
    """
    bias = np.zeros(3)

    for axis, name in enumerate(("x", "y", "z")):
        positive = validate_xyz(stationary_recordings[name + "+"])
        negative = validate_xyz(stationary_recordings[name + "-"])

        bias[axis] = (
            positive[:, axis].mean() + negative[:, axis].mean()
        ) / 2.0

    return bias


def rotate_axes(xyz, rotation=None):
    """
    Apply a fixed proper rotation:
        output_vector = rotation @ input_vector

    This maps mounting coordinates; it does not estimate orientation.
    """
    xyz = validate_xyz(xyz)
    if rotation is None:
        return xyz.copy()

    rotation = np.asarray(rotation, dtype=np.float64)

    if (
        rotation.shape != (3, 3)
        or not np.isfinite(rotation).all()
        or not np.allclose(rotation.T @ rotation, np.eye(3), atol=1e-6)
        or not np.isclose(np.linalg.det(rotation), 1.0, atol=1e-6)
    ):
        raise ValueError("rotation must be a proper 3x3 rotation matrix.")

    return xyz @ rotation.T


def lowpass_filter(xyz, sample_rate_hz, cutoff_hz=20.0, order=4):
    """
    Causal Butterworth low-pass filter.

    Initialize at the first reading to avoid a zero-state transient.
    Filter the stationary prefix and gesture together, then crop.
    """
    from scipy.signal import sosfilt_zi

    xyz = validate_xyz(xyz)

    if not np.isfinite(sample_rate_hz) or sample_rate_hz <= 0:
        raise ValueError("sample_rate_hz must be positive.")
    if not 0 < cutoff_hz < sample_rate_hz / 2:
        raise ValueError("Cutoff must be below the Nyquist frequency.")

    sos = butter(
        order, cutoff_hz, btype="lowpass",
        fs=sample_rate_hz, output="sos"
    )

    filtered = np.empty_like(xyz)
    for axis in range(3):
        zi = sosfilt_zi(sos) * xyz[0, axis]
        filtered[:, axis], _ = sosfilt(sos, xyz[:, axis], zi=zi)

    return filtered


def crop_gesture(xyz, start=0, stop=None):
    """Crop using sample indices; stop is exclusive."""
    xyz = validate_xyz(xyz)
    stop = len(xyz) if stop is None else stop

    if not 0 <= start < stop <= len(xyz):
        raise ValueError("Invalid gesture boundaries.")

    return validate_xyz(xyz[start:stop])


def resample_gesture(xyz, target_length=64):
    """
    Normalize a complete gesture to fixed length.

    Applies additional smoothing before substantial reduction.
    Output time represents gesture progress, not absolute duration.
    """
    xyz = validate_xyz(xyz)
    if target_length < 2:
        raise ValueError("target_length must be at least 2.")

    old_time = np.linspace(0.0, 1.0, len(xyz))
    new_time = np.linspace(0.0, 1.0, target_length)

    if len(xyz) > target_length:
        # Units here are samples per unit of normalized gesture time.
        # Keep the cutoff below the target sequence's Nyquist limit.
        xyz = lowpass_filter(
            xyz,
            sample_rate_hz=len(xyz) - 1,
            cutoff_hz=0.4 * (target_length - 1),
        )

    return np.column_stack([
        np.interp(new_time, old_time, xyz[:, axis])
        for axis in range(3)
    ]).astype(np.float32)


def preprocess_recording(
    raw_xyz,
    sample_rate_hz,
    target_length=64,
    bias_counts=None,
    rotation=None,
    cutoff_hz=20.0,
    gesture_start=0,
    gesture_stop=None,
):
    """
    Raw recording → calibrated, filtered, fixed-length gesture.

    Keep gravity. Include a stationary prefix in raw_xyz and exclude
    it using gesture_start after filtering.
    """
    xyz = counts_to_g(raw_xyz, bias_counts)
    xyz = rotate_axes(xyz, rotation)
    xyz = lowpass_filter(xyz, sample_rate_hz, cutoff_hz)
    xyz = crop_gesture(xyz, gesture_start, gesture_stop)
    return resample_gesture(xyz, target_length)


def fit_normalizer(training_gestures, std_floor=1e-3):
    """
    Fit only on training gestures of shape (examples, time, 3).
    std_floor is expressed in g.
    """
    data = np.asarray(training_gestures, dtype=np.float32)

    if (
        data.ndim != 3 or data.shape[-1] != 3
        or data.shape[0] == 0 or data.shape[1] < 2
        or not np.isfinite(data).all()
    ):
        raise ValueError("Expected finite training data (examples, time, 3).")
    if std_floor <= 0:
        raise ValueError("std_floor must be positive.")

    return {
        "mean": data.mean(axis=(0, 1)),
        "std": np.maximum(data.std(axis=(0, 1)), std_floor),
    }


def normalize(data, statistics):
    """Apply saved training statistics to gestures or batches."""
    data = np.asarray(data, dtype=np.float32)
    mean = np.asarray(statistics["mean"], dtype=np.float32)
    std = np.asarray(statistics["std"], dtype=np.float32)

    if data.shape[-1] != 3 or mean.shape != (3,) or std.shape != (3,):
        raise ValueError("Expected three channels.")
    if (
        not np.isfinite(data).all()
        or not np.isfinite(mean).all()
        or not np.isfinite(std).all()
        or np.any(std <= 0)
    ):
        raise ValueError("Invalid data or normalization statistics.")

    return ((data - mean) / std).astype(np.float32)

def prepare_files(files):
    return np.stack([
        preprocess_recording(
            load_csv(path),
            sample_rate_hz=SAMPLING_RATE_HZ,
            target_length=128,
            cutoff_hz=20.0,
            # Set these for each recording if it includes a prefix:
            gesture_start=0,
            gesture_stop=None,
        )
        for path in files
    ])
    
    


def discover_recordings(dataset_dir, class_names=CLASS_NAMES):
    """
    Discover dataset/<class_name>/*.txt for the given classes.

    Returns:
        records: list of (path, integer_label)
        class_names: labels in the given order (label i -> class_names[i])
    """
    root = Path(dataset_dir)

    if not root.is_dir():
        raise ValueError(f"Dataset directory does not exist: {root}")

    class_names = list(class_names)
    if len(class_names) < 2:
        raise ValueError("Expected at least two classes.")

    missing = [
        name for name in class_names
        if not any((root / name).glob("*.txt"))
    ]
    if missing:
        raise ValueError(f"No .txt recordings found for classes: {missing}")

    records = []

    for label, class_name in enumerate(class_names):
        files = sorted((root / class_name).glob("*.txt"))
        records.extend((path, label) for path in files)
        print(f"{class_name}: {len(files)} recordings")

    return records, class_names


def split_recordings(
    records,
    train_fraction=0.60,
    validation_fraction=0.20,
    seed=42,
):
    """
    Stratified split by recording.

    Use only when recordings are independent. If participants or
    sessions repeat, replace this with a participant/session split.
    """
    if (
        train_fraction <= 0
        or validation_fraction <= 0
        or train_fraction + validation_fraction >= 1
    ):
        raise ValueError("Fractions must leave nonempty test data.")

    rng = np.random.default_rng(seed)
    splits = {"train": [], "validation": [], "test": []}

    labels = sorted({label for _, label in records})

    for label in labels:
        class_records = [
            record for record in records if record[1] == label
        ]

        indices = rng.permutation(len(class_records))
        shuffled = [class_records[i] for i in indices]

        n_train = int(len(shuffled) * train_fraction)
        n_validation = int(len(shuffled) * validation_fraction)

        if min(
            n_train,
            n_validation,
            len(shuffled) - n_train - n_validation,
        ) < 1:
            raise ValueError(f"Insufficient recordings for class {label}.")

        splits["train"].extend(shuffled[:n_train])
        splits["validation"].extend(
            shuffled[n_train:n_train + n_validation]
        )
        splits["test"].extend(
            shuffled[n_train + n_validation:]
        )

    for name, subset in splits.items():
        order = rng.permutation(len(subset))
        splits[name] = [subset[i] for i in order]

    return splits


def preprocess_split(
    records,
    sample_rate_hz,
    target_length=64,
    cutoff_hz=20.0,
    bias_counts=None,
    rotation=None,
    expected_duration_seconds=2.0,
    sample_count_tolerance=0.05,
):
    """
    Load one recording per file and apply the previous preprocessing.

    Assumes each file has an ax,ay,az header followed by signed counts.
    The sample-count check cannot verify sensor freshness.
    """
    gestures = []
    labels = []
    filenames = []

    expected_count = (
        sample_rate_hz * expected_duration_seconds
    )

    for path, label in records:
        raw = load_csv(path)

        relative_difference = (
            abs(len(raw) - expected_count) / expected_count
        )

        if relative_difference > sample_count_tolerance:
            raise ValueError(
                f"{path}: found {len(raw)} rows; expected approximately "
                f"{expected_count:.0f} for "
                f"{expected_duration_seconds}s at {sample_rate_hz}Hz."
            )

        gesture = preprocess_recording(
            raw,
            sample_rate_hz=sample_rate_hz,
            target_length=target_length,
            cutoff_hz=cutoff_hz,
            bias_counts=bias_counts,
            rotation=rotation,
        )

        gestures.append(gesture)
        labels.append(label)
        filenames.append(str(path))

    return (
        np.stack(gestures),
        np.asarray(labels, dtype=np.int64),
        np.asarray(filenames),
    )


def prepare_dataset(
    dataset_dir=DEFAULT_DATASET_DIR,
    sample_rate_hz=SAMPLING_RATE_HZ,
    output_path=DEFAULT_OUTPUT_PATH,
    class_names=CLASS_NAMES,
    target_length=64,
    cutoff_hz=20.0,
    train_fraction=0.60,
    validation_fraction=0.20,
    expected_duration_seconds=2.1,
    sample_count_tolerance=0.05,
    seed=42,
):
    """
    Load → preprocess → shuffle/split → normalize → save.

    Recordings are shuffled and split per class (stratified), then each
    split is shuffled again so classes are interleaved. Normalization
    statistics are fitted on the training split only.
    """
    records, class_names = discover_recordings(dataset_dir, class_names)
    splits = split_recordings(
        records,
        train_fraction=train_fraction,
        validation_fraction=validation_fraction,
        seed=seed,
    )

    prepared = {
        name: preprocess_split(
            subset,
            sample_rate_hz=sample_rate_hz,
            target_length=target_length,
            cutoff_hz=cutoff_hz,
            expected_duration_seconds=expected_duration_seconds,
            sample_count_tolerance=sample_count_tolerance,
        )
        for name, subset in splits.items()
    }

    # Fit exclusively on the training set.
    statistics = fit_normalizer(prepared["train"][0])

    saved = {
        "class_names": np.asarray(class_names),
        "mean": statistics["mean"],
        "std": statistics["std"],
        "sample_rate_hz": np.asarray(sample_rate_hz),
        "counts_per_g": np.asarray(COUNTS_PER_G),
        "target_length": np.asarray(target_length),
        "cutoff_hz": np.asarray(cutoff_hz),
    }

    for name, (features, labels, filenames) in prepared.items():
        saved[f"X_{name}"] = normalize(features, statistics)
        saved[f"y_{name}"] = labels
        saved[f"files_{name}"] = filenames

    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(output_path, **saved)

    print("Class mapping:", dict(enumerate(class_names)))
    for name in splits:
        counts = np.bincount(saved[f"y_{name}"], minlength=len(class_names))
        print(
            f"{name}: X={saved[f'X_{name}'].shape}, "
            f"y={saved[f'y_{name}'].shape}, "
            f"per class={dict(zip(class_names, counts.tolist()))}"
        )
    print(f"Saved dataset to {output_path}")

    return saved


def load_prepared_dataset(path=DEFAULT_OUTPUT_PATH, one_hot=False):
    """
    Load a dataset saved by prepare_dataset.

    Returns a dict with:
        "train"/"validation"/"test": (X, y), X shape (N, time, 3)
        "class_names": list of label names
        "mean"/"std": normalization statistics (apply to new data)
    """
    with np.load(path, allow_pickle=False) as data:
        class_names = [str(name) for name in data["class_names"]]
        dataset = {
            "class_names": class_names,
            "mean": data["mean"],
            "std": data["std"],
        }
        for name in ("train", "validation", "test"):
            X = data[f"X_{name}"].astype(np.float32)
            y = data[f"y_{name}"].astype(np.int64)
            if one_hot:
                y = np.eye(len(class_names), dtype=np.float32)[y]
            dataset[name] = (X, y)

    return dataset


def make_tf_datasets(path=DEFAULT_OUTPUT_PATH, batch_size=16, seed=42,
                     one_hot=False):
    """
    Build batched tf.data pipelines for training.

    The training set is reshuffled every epoch; validation and test are
    kept in a fixed order.
    """
    import tensorflow as tf

    data = load_prepared_dataset(path, one_hot=one_hot)
    pipelines = {}

    for name in ("train", "validation", "test"):
        X, y = data[name]
        ds = tf.data.Dataset.from_tensor_slices((X, y))
        if name == "train":
            ds = ds.shuffle(len(X), seed=seed, reshuffle_each_iteration=True)
        pipelines[name] = ds.batch(batch_size).prefetch(tf.data.AUTOTUNE)

    return pipelines, data["class_names"]


def parse_args():
    parser = argparse.ArgumentParser(
        description="Prepare the IMU gesture classification dataset."
    )
    parser.add_argument("--dataset-dir", type=Path,
                        default=DEFAULT_DATASET_DIR)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT_PATH)
    parser.add_argument("--sample-rate", type=float,
                        default=SAMPLING_RATE_HZ)
    parser.add_argument("--target-length", type=int, default=128)
    parser.add_argument("--cutoff", type=float, default=20.0)
    parser.add_argument("--train-fraction", type=float, default=0.70)
    parser.add_argument("--validation-fraction", type=float, default=0.20)
    parser.add_argument("--duration", type=float, default=2.1,
                        help="Expected recording duration in seconds.")
    parser.add_argument("--seed", type=int, default=42)
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()

    prepare_dataset(
        dataset_dir=args.dataset_dir,
        sample_rate_hz=args.sample_rate,
        output_path=args.output,
        class_names=CLASS_NAMES,
        target_length=args.target_length,
        cutoff_hz=args.cutoff,
        train_fraction=args.train_fraction,
        validation_fraction=args.validation_fraction,
        expected_duration_seconds=args.duration,
        seed=args.seed,
    )
