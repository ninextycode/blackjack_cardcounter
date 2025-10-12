import numpy as np
import tensorflow as tf
from tensorflow import keras
import cv2


_model = None  # Class variable to store the loaded model
_emnist_labels = list('0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz')  # 62 classes

def load_model(model_path='extern/OCR-ITSP-2018/OCR/97_5_92.h5'):
    global _model
    if _model is None:
        try:
            _model = keras.models.load_model(model_path)
            print("Model loaded successfully.")
        except Exception as e:
            raise ValueError(f"Failed to load model from {model_path}: {str(e)}")
    return _model


def predict(img_array, limit_characters=None):
    """
    Predict the character in a grayscale NumPy array using the EMNIST model.

    Args:
        img_array (np.ndarray): Grayscale image (shape: [height, width], uint8 or float).
        limit_characters (Iterable[str] | None): Optional collection of characters to
            constrain the prediction to. When provided and non-empty, the argmax is
            computed only over these characters.

    Returns:
        str: Predicted character (0-9, A-Z, a-z).
    """
    # Ensure model is loaded
    model = load_model()

    # Preprocess image
    # Resize to 28x28 using OpenCV
    img_resized = cv2.resize(img_array, (28, 28), interpolation=cv2.INTER_AREA)
    # Normalize to [0, 1]
    if img_array.dtype == np.uint8:
        img_normalized = img_resized.astype(np.float32) / 255.0
    else:
        img_normalized = img_resized.astype(np.float32)
        img_normalized = np.clip(img_normalized, 0, 1)  # Ensure [0, 1] range
    # Reshape to [1, 28, 28, 1] for model input
    img_input = img_normalized.reshape(1, 28, 28, 1)

    # Predict
    prediction = model.predict(img_input, verbose=0)  # shape: (1, num_classes)
    probs = prediction[0]

    # If a character subset is provided, restrict argmax to it
    if limit_characters is not None:
        allowed = set(limit_characters)
        allowed_idx = [i for i, lbl in enumerate(_emnist_labels) if lbl in allowed]
        print(probs.shape)
        # Argmax only over the allowed indices
        sub_probs = probs[allowed_idx]
        local_idx = int(np.argmax(sub_probs))
        class_id = allowed_idx[local_idx]
    else:
        class_id = int(np.argmax(probs))

    return _emnist_labels[class_id]
