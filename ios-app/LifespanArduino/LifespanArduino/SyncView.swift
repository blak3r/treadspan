import SwiftUI
import CoreBluetooth
import HealthKit

struct SyncView: View {
    @StateObject private var viewModel = BLEViewModel()

    var body: some View {
        VStack(spacing: 20) {
            Circle()
                .fill(Color.gray)
                .frame(width: 100, height: 100)

            Text("BetterSpan Fit")
                .font(.largeTitle)
                .fontWeight(.bold)

            Button(action: {
                viewModel.fetchAndSaveSessions()
            }) {
                Text("Fetch/Save Sessions")
                    .font(.title2)
                    .padding()
                    .frame(maxWidth: .infinity)
                    .background(Color.blue)
                    .foregroundColor(.white)
                    .cornerRadius(10)
            }
            .padding(.horizontal)

            Text(viewModel.statusMessage)
                .font(.body)
                .foregroundColor(.gray)
                .padding(.horizontal)

            if !viewModel.sessions.isEmpty {
                List(viewModel.sessions) { session in
                    Text(session.displayString)
                }
                .listStyle(PlainListStyle())

                Button(action: {
                    viewModel.saveToHealthKit()
                }) {
                    Text("Save Sessions to HealthKit")
                        .font(.title3)
                        .padding()
                        .frame(maxWidth: .infinity)
                        .background(Color.green)
                        .foregroundColor(.white)
                        .cornerRadius(10)
                }
                .padding(.horizontal)
            }

            Spacer()
        }
        .padding()
    }
}

struct Session: Identifiable {
    let id = UUID()
    let start: UInt32
    let stop: UInt32
    let steps: UInt32

    var displayString: String {
        let startDate = Date(timeIntervalSince1970: TimeInterval(start))
        let stopDate  = Date(timeIntervalSince1970: TimeInterval(stop))
        let durationSec = Int(stop - start)

        let dateFormatter = DateFormatter()
        dateFormatter.dateFormat = "MM/dd/yyyy H:mm:ss"

        let startString = dateFormatter.string(from: startDate)
        let durationStr = Self.humanize(seconds: durationSec)

        return "\(startString), \(durationStr) for \(steps) steps"
    }

    private static func humanize(seconds: Int) -> String {
        if seconds < 60 {
            return "\(seconds) sec"
        }
        let minutes = seconds / 60
        if minutes < 60 {
            return "\(minutes) min"
        }
        let hours = minutes / 60
        let remainMinutes = minutes % 60
        if remainMinutes == 0 {
            return "\(hours)h"
        } else {
            return "\(hours)h \(remainMinutes)min"
        }
    }
}

class BLEViewModel: NSObject, ObservableObject {
    @Published var statusMessage: String = "Idle"
    @Published var sessions: [Session] = []

    private var centralManager: CBCentralManager!
    private var peripheral: CBPeripheral?

    private var dataCharacteristic: CBCharacteristic?
    private var confirmCharacteristic: CBCharacteristic?

    private let serviceUUID = CBUUID(string: "12345678-1234-5678-1234-56789abcdef0")
    private let dataCharUUID = CBUUID(string: "12345678-1234-5678-1234-56789abcdef1")
    private let confirmCharUUID = CBUUID(string: "12345678-1234-5678-1234-56789abcdef2")

    private var fetchedSessions: [Session] = []

    // HealthKit
    private let healthStore = HKHealthStore()
    private let stepType = HKObjectType.quantityType(forIdentifier: .stepCount)!

    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: nil)
    }

    func fetchAndSaveSessions() {
        guard centralManager.state == .poweredOn else {
            statusMessage = "Bluetooth not ready."
            return
        }

        statusMessage = "Scanning for Treadmill..."
        fetchedSessions.removeAll()
        sessions.removeAll()

        centralManager.scanForPeripherals(withServices: [serviceUUID], options: nil)

        DispatchQueue.main.asyncAfter(deadline: .now() + 10) {
            self.centralManager.stopScan()
            if self.peripheral == nil {
                self.statusMessage = "No Treadmill found."
            }
        }
    }

    private func processFetchedData(_ data: Data) {
        print("Raw data: \(data.map { String(format: "%02X", $0) }.joined()) (length: \(data.count))")
        guard data.count == 12 else {
            print("Invalid data length. Expected 12, got \(data.count).")
            return
        }

        let start = data.subdata(in: 0..<4).withUnsafeBytes { $0.load(as: UInt32.self).bigEndian }
        let stop  = data.subdata(in: 4..<8).withUnsafeBytes { $0.load(as: UInt32.self).bigEndian }
        let steps = data.subdata(in: 8..<12).withUnsafeBytes { $0.load(as: UInt32.self).bigEndian }

        let session = Session(start: start, stop: stop, steps: steps)
        fetchedSessions.append(session)
        sessions = fetchedSessions

        print("Parsed session: Start=\(start), Stop=\(stop), Steps=\(steps)")
    }

    private func confirmSession() {
        guard let confirmChar = confirmCharacteristic,
              let peripheral = peripheral else {
            print("Cannot confirm session: Missing characteristic/peripheral.")
            return
        }
        let ackData = Data([0x01])
        peripheral.writeValue(ackData, for: confirmChar, type: .withResponse)
        print("Attempting to write 0x01 to confirmCharacteristic...")
    }

    // MARK: - HealthKit Step Saving
    func saveToHealthKit() {
        // 1) Check if Health Data is available on this device
        guard HKHealthStore.isHealthDataAvailable() else {
            print("Health data not available on this device.")
            return
        }

        // 2) Request authorization if needed
        let writeTypes: Set<HKSampleType> = [stepType] // We only want to write steps
        healthStore.requestAuthorization(toShare: writeTypes, read: nil) { [weak self] success, error in
            guard let self = self else { return }

            if let error = error {
                print("HK authorization error: \(error.localizedDescription)")
                return
            }
            if !success {
                print("User did not grant HealthKit authorization.")
                return
            }

            // 3) Save each session as an HKQuantitySample for stepCount
            self.saveAllSessionsAsSteps()
        }
    }

    private func saveAllSessionsAsSteps() {
        guard !sessions.isEmpty else {
            print("No sessions to save.")
            return
        }

        for session in sessions {
            let startDate = Date(timeIntervalSince1970: TimeInterval(session.start))
            let endDate   = Date(timeIntervalSince1970: TimeInterval(session.stop))
            let quantity  = HKQuantity(unit: .count(), doubleValue: Double(session.steps))

            let stepSample = HKQuantitySample(type: stepType,
                                              quantity: quantity,
                                              start: startDate,
                                              end: endDate)

            healthStore.save(stepSample) { success, error in
                if success {
                    print("Saved \(session.steps) steps to HealthKit from \(startDate) - \(endDate)")
                } else {
                    print("Failed saving steps: \(String(describing: error?.localizedDescription))")
                }
            }
        }
    }
}

// MARK: - CBCentralManagerDelegate
extension BLEViewModel: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            statusMessage = "Bluetooth is ON"
        case .poweredOff:
            statusMessage = "Bluetooth is OFF"
        case .resetting:
            statusMessage = "Bluetooth is resetting..."
        case .unauthorized:
            statusMessage = "Bluetooth unauthorized."
        case .unknown:
            statusMessage = "Bluetooth state unknown."
        case .unsupported:
            statusMessage = "Bluetooth not supported."
        @unknown default:
            statusMessage = "Unknown Bluetooth state."
        }
    }

    func centralManager(_ central: CBCentralManager,
                        didDiscover peripheral: CBPeripheral,
                        advertisementData: [String : Any],
                        rssi RSSI: NSNumber) {
        print("Discovered device: \(peripheral.name ?? "Unknown") - \(peripheral.identifier)")
        centralManager.stopScan()

        self.peripheral = peripheral
        self.peripheral?.delegate = self
        centralManager.connect(peripheral, options: nil)

        statusMessage = "Connecting to \(peripheral.name ?? "Treadmill")..."
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        statusMessage = "Connected to \(peripheral.name ?? "Treadmill")"
        print("CentralManager: connected to \(peripheral.name ?? "(no name)")")

        peripheral.discoverServices([serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        statusMessage = "Failed to connect."
        self.peripheral = nil
        print("Connection failed: \(error?.localizedDescription ?? "Unknown error")")
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        statusMessage = "Disconnected."
        self.peripheral = nil
        print("Disconnected: \(error?.localizedDescription ?? "no error")")
    }
}

// MARK: - CBPeripheralDelegate
extension BLEViewModel: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error = error {
            print("Error discovering services: \(error.localizedDescription)")
            statusMessage = "Error discovering services."
            return
        }
        guard let services = peripheral.services else { return }

        for service in services where service.uuid == serviceUUID {
            print("Found Treadmill service, discovering characteristics...")
            peripheral.discoverCharacteristics([dataCharUUID, confirmCharUUID], for: service)
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error = error {
            print("Error discovering characteristics: \(error.localizedDescription)")
            statusMessage = "Error discovering characteristics."
            return
        }
        guard let characteristics = service.characteristics else { return }

        for characteristic in characteristics {
            if characteristic.uuid == dataCharUUID {
                dataCharacteristic = characteristic
                // For indicates or notifies
                peripheral.setNotifyValue(true, for: characteristic)
                statusMessage = "Fetching sessions..."
                print("Data characteristic found. Attempting to enable notifications/indications.")
            } else if characteristic.uuid == confirmCharUUID {
                confirmCharacteristic = characteristic
                print("Confirmation characteristic found.")
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        if let error = error {
            print("Error updating notification state for \(characteristic.uuid): \(error.localizedDescription)")
            return
        }
        if characteristic.isNotifying {
            print("Indications/Notifications enabled for \(characteristic.uuid)")
        } else {
            print("Indications/Notifications stopped for \(characteristic.uuid)")
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error = error {
            print("Error updating value for \(characteristic.uuid): \(error.localizedDescription)")
            return
        }
        guard let value = characteristic.value else {
            print("No value for \(characteristic.uuid)")
            return
        }

        if characteristic.uuid == dataCharUUID {
            print("Received indicated data: \(value.map { String(format: "%02X", $0) }.joined())")
            processFetchedData(value)
            confirmSession()
            statusMessage = "Session acknowledged, waiting for next..."
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error = error {
            print("Error writing value for \(characteristic.uuid): \(error.localizedDescription)")
        } else {
            print("Successfully wrote to \(characteristic.uuid)")
        }
    }
}
