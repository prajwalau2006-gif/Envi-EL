import logging
from flask import Flask, request, jsonify, render_template

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

app = Flask(__name__)

# Global state initialized with starting metrics
data_state = {
    "ppm": 0,
    "fan": "OFF",
    "buzzer": "OFF"
}

@app.route("/")
def index():
    """Render the dashboard UI."""
    return render_template("index.html")

@app.route("/update", methods=["POST"])
def update():
    """
    Update the server data state from ESP32 JSON payload.
    Expected format: {"ppm": <int>, "fan": <int>, "buzzer": <int>}
    """
    global data_state
    try:
        content = request.get_json(silent=True)
        if not content:
            logging.warning("Received update request with empty or invalid JSON payload.")
            return jsonify({"status": "error", "message": "Invalid JSON"}), 400

        # Retrieve and validate data
        ppm_val = content.get("ppm")
        fan_val = content.get("fan")
        buzzer_val = content.get("buzzer")

        if ppm_val is None or fan_val is None or buzzer_val is None:
            logging.warning(f"Missing required fields in payload: {content}")
            return jsonify({"status": "error", "message": "Missing required fields"}), 400

        # Map metrics
        try:
            ppm = int(ppm_val)
        except (ValueError, TypeError):
            ppm = 0

        # Convert 0/1 integers to clean "ON"/"OFF" strings
        fan = "ON" if fan_val == 1 else "OFF"
        buzzer = "ON" if buzzer_val == 1 else "OFF"

        # Update global state
        data_state["ppm"] = ppm
        data_state["fan"] = fan
        data_state["buzzer"] = buzzer

        logging.info(f"Successfully updated state: {data_state}")
        return jsonify({"status": "success", "data": data_state}), 200

    except Exception as e:
        logging.error(f"Error during state update: {str(e)}")
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route("/data", methods=["GET"])
def get_data():
    """Retrieve the current data state."""
    return jsonify(data_state), 200

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
