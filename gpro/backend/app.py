from flask import Flask, request, jsonify, send_from_directory, send_file
from flask_cors import CORS
import time
import os
import pandas as pd
import matplotlib.pyplot as plt
import io

app = Flask(__name__, static_folder="../frontend", static_url_path="/")
CORS(app)

# Simulated global state
status_data = {
    "status": "Waiting...",
    "elapsed_time": 0,
    "average": 0,
    "finished": False,
    "result": "",
    "params": {},
    "sensor_data": []
}

@app.route('/')
def serve_react():
    return send_from_directory(app.static_folder, 'index.html')

@app.route('/<path:filename>')
def serve_static(filename):
    try:
        return send_from_directory(app.static_folder, filename)
    except FileNotFoundError:
        return jsonify({"error": "File not found"}), 404

@app.route('/api/post', methods=['POST'])
def receive_sensor_data():
    data = request.get_json()
    if not data or 'rx' not in data or 'tx' not in data or 'time' not in data: # Added 'time' check
        return jsonify({"error": "Invalid data, missing time, rx, or tx"}), 400
    
    # Ensure rx is a list of numbers
    if not isinstance(data["rx"], list) or not all(isinstance(val, (int, float)) for val in data["rx"]):
        return jsonify({"error": "Invalid 'rx' format, expected list of numbers"}), 400

    status_data["sensor_data"].append({
        "time": data["time"],
        "tx": data["tx"],
        "rx": data["rx"]
    })
    return jsonify({"message": "Data received"}), 200

@app.route('/api/start', methods=['POST'])
def start_test():
    data = request.get_json()
    classification_type = data.get("classification_type")
    cycles = int(data.get("cycles", 3))
    duration = int(data.get("duration", 5)) # Duration of each touch/untouch phase
    soft_threshold = int(data.get("soft_threshold", 350))
    fresh_threshold = int(data.get("fresh_threshold", 750))

    status_data["status"] = f"Running {classification_type.replace('_', ' ').title()} Test..."
    status_data["elapsed_time"] = 0
    status_data["average"] = 0
    status_data["finished"] = False
    status_data["result"] = ""
    status_data["sensor_data"] = [] # Clear previous data on start
    status_data["params"] = {
        "type": classification_type,
        "soft_threshold": soft_threshold,
        "fresh_threshold": fresh_threshold,
        "cycles": cycles,
        "duration": duration # Duration per phase (e.g., 5 seconds for touch, 5 for untouch)
    }
    return jsonify({"message": "Test started"})

@app.route('/api/stop', methods=['POST'])
def stop_test():
    status_data["status"] = "Stopped by user"
    status_data["finished"] = True
    return jsonify({"message": "Test stopped"})

@app.route('/api/status')
def get_status():
    if not status_data["finished"]:
        # Increment elapsed_time based on a 'tick' or actual time passed if available
        # For simplicity, assuming this is called every second by frontend
        status_data["elapsed_time"] += 1

        # Compute average RX across all *individual* rx values collected so far
        if status_data["sensor_data"]:
            # Flatten all rx lists into a single list of numbers
            all_individual_rx_values = [val for d in status_data["sensor_data"] for val in d["rx"]]
            status_data["average"] = sum(all_individual_rx_values) / len(all_individual_rx_values) if all_individual_rx_values else 0
        else:
            status_data["average"] = 0

        # Update current phase (touch or untouch)
        # Total cycle time for one touch + one untouch phase
        cycle_time = status_data["params"].get("duration", 5) * 2 # e.g., 5s touch + 5s untouch = 10s
        current_second_in_cycle = status_data["elapsed_time"] % cycle_time
        
        # Determine current phase based on elapsed time within a cycle
        phase_duration = status_data["params"].get("duration", 5)
        phase = "Touch" if current_second_in_cycle < phase_duration else "Untouch"
        
        # Update status message with current phase and test type
        test_type_display = status_data['params']['type'].replace('_', ' ').title()
        status_data["status"] = f"{phase} Phase | Running {test_type_display} Test..."

        # Finish after total test duration
        total_cycles = status_data["params"].get("cycles", 3)
        total_test_duration = total_cycles * cycle_time # Total time for all cycles
        
        if status_data["elapsed_time"] >= total_test_duration:
            status_data["finished"] = True
            if status_data["sensor_data"]:
                avg = status_data["average"]
                ctype = status_data["params"]["type"]
                result = ""
                
                # Determine result based on classification type and average RX
                if ctype == "fresh_rotten":
                    if avg > status_data["params"]["fresh_threshold"]:
                        result = "Rotten"
                    else:
                        result = "Fresh"
                elif ctype == "soft_hard":
                    if avg > status_data["params"]["soft_threshold"]:
                        result = "Hard"
                    else:
                        result = "Soft"
                else:
                    result = "Unknown Classification Type" # Handle unexpected types

                status_data["status"] = "Test Completed"
                status_data["result"] = result
            else:
                status_data["status"] = "No sensor data received. Test could not be completed."
                status_data["result"] = "No Result"

    return jsonify({
        "status": status_data["status"],
        "elapsed_time": status_data["elapsed_time"],
        "average": round(status_data["average"], 2),
        "finished": status_data["finished"],
        "result": status_data["result"]
    })

@app.route('/api/plot_img')
def plot_img():
    if not status_data["sensor_data"]:
        return jsonify({"error": "No data available"}), 404

    # Prepare data for plotting: each RX channel as a separate column over time
    all_rx_readings_for_df = []
    for entry in status_data["sensor_data"]:
        row_dict = {"time": entry["time"]}
        for i, rx_val in enumerate(entry["rx"]):
            row_dict[f"RX{i}"] = rx_val
        all_rx_readings_for_df.append(row_dict)

    df_plot = pd.DataFrame(all_rx_readings_for_df)

    plt.figure(figsize=(12, 7)) # Adjust figure size for better readability

    # Determine x-axis: use 'time' if available, otherwise use DataFrame index
    # It's crucial that 'time' values from Arduino are unique and incrementally increasing
    x_axis_label = "Time (from Arduino)"
    if 'time' in df_plot.columns and pd.api.types.is_numeric_dtype(df_plot['time']):
        x_axis_data = df_plot['time']
    else:
        x_axis_data = df_plot.index
        x_axis_label = "Sample Index" # Fallback if 'time' is not numeric or not present

    # Plot each RX column
    # Assuming RX0 to RX6, modify range if number of RX channels changes
    for i in range(7): 
        col_name = f"RX{i}"
        if col_name in df_plot.columns: # Check if the column exists
            plt.plot(x_axis_data, df_plot[col_name], label=col_name, marker='.', markersize=4, linestyle='-')

    plt.xlabel(x_axis_label)
    plt.ylabel("Sensor Value")
    plt.title("Individual Sensor Data Over Time (All RX Channels)")
    plt.legend(loc='best') # Place legend in the best possible position
    plt.grid(True) # Add grid for better readability
    plt.tight_layout() # Adjust layout to prevent labels from overlapping

    buf = io.BytesIO()
    plt.savefig(buf, format="png")
    plt.close() # Close the plot to free up memory
    buf.seek(0)
    return send_file(buf, mimetype="image/png")

@app.route('/api/download_csv')
def download_csv():
    if not status_data["sensor_data"]:
        return jsonify({"error": "No data available"}), 404
    
    # This structure correctly flattens the RX list into multiple columns (rx0, rx1, ...)
    # Each dictionary in the list becomes a row in the DataFrame
    df = pd.DataFrame([
        {"time": d["time"], "tx": d["tx"], **{f"rx{i}": val for i, val in enumerate(d["rx"])}}
        for d in status_data["sensor_data"]
    ])
    
    csv = df.to_csv(index=False)
    return send_file(
        io.BytesIO(csv.encode()),
        mimetype="text/csv",
        as_attachment=True,
        download_name="sensor_data.csv"
    )

@app.route('/hardness')
def serve_hardness():
    return send_from_directory(app.static_folder, 'hardness.html')

@app.route('/fruit')
def serve_fruit():
    return send_from_directory(app.static_folder, 'fruit.html')

@app.route('/download')
def serve_download():
    return send_from_directory(app.static_folder, 'download.html')

if __name__ == '__main__':
    port = int(os.environ.get("PORT", 5000))
    app.run(debug=False, host='0.0.0.0', port=port)