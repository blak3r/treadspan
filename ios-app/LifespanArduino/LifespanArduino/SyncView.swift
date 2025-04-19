import SwiftUI
import CoreBluetooth
import HealthKit

struct SyncView: View {
    @StateObject private var viewModel = BLEViewModel()

    var body: some View {
        ZStack {
            VStack(spacing: 20) {
                Image("walking-pad-v1") // Use your image asset name
                    .resizable()
                    .scaledToFit()
                    .frame(width: 100, height: 100)
                    .clipShape(Circle())
                    .overlay(Circle().stroke(Color.white, lineWidth: 2))
                    .shadow(radius: 5)

                Text("TreadSpan")
                    .font(.system(.largeTitle, design: .default))
                    .fontWeight(.heavy)
                    .tracking(3)
                    .textCase(.uppercase)

                // Fetch/Save Sessions Button with busy indicator
                Button(action: {
                    viewModel.fetchAndSaveSessions()
                }) {
                    if viewModel.isFetching {
                        HStack {
                            ProgressView()
                            Text("Scanning...")
                        }
                        .font(.title2)
                    } else {
                        Text("Sync Sessions")
                            .font(.title2)
                    }
                }
                .padding()
                .frame(maxWidth: .infinity)
                .background(viewModel.isFetching ? Color.gray : Color.blue)
                .foregroundColor(.white)
                .cornerRadius(10)
                .padding(.horizontal)
                .disabled(viewModel.isFetching)

                // Status message
                Text(viewModel.statusMessage)
                    .font(.body)
                    .foregroundColor(.gray)
                    .padding(.horizontal)

                // Sessions List
                if !viewModel.sessions.isEmpty {
                    List {
                        ForEach(viewModel.sessions) { session in
                            SessionRowView(
                                session: session,
                                synced: viewModel.healthKitSyncCompleted
                            )
                        }
                    }
                    .listStyle(InsetGroupedListStyle())
                    .frame(maxHeight: 300)
                }

                Spacer()
            }
            .padding()
            .toast(message: $viewModel.healthKitSyncStatusMessage)
            .onAppear {
                // Automatically scan/fetch from the treadmill when this view appears
                viewModel.fetchAndSaveSessions()
            }
        }
    }
}

// MARK: - Toast Modifier

struct ToastModifier: ViewModifier {
    @Binding var message: String

    func body(content: Content) -> some View {
        ZStack {
            content
            if !message.isEmpty {
                VStack {
                    Spacer()
                    Text(message)
                        .padding()
                        .background(Color.black.opacity(0.8))
                        .foregroundColor(.white)
                        .cornerRadius(8)
                        .padding(.bottom, 20)
                        .transition(.opacity)
                        .onAppear {
                            DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
                                withAnimation {
                                    message = ""
                                }
                            }
                        }
                }
            }
        }
    }
}

extension View {
    func toast(message: Binding<String>) -> some View {
        self.modifier(ToastModifier(message: message))
    }
}

// MARK: - Session Row with Custom Layout

struct SessionRowView: View {
    let session: Session
    let synced: Bool

    private var startDate: Date {
        Date(timeIntervalSince1970: TimeInterval(session.start))
    }

    private var dateString: String {
        let formatter = DateFormatter()
        formatter.dateStyle = .medium
        formatter.timeStyle = .none
        return formatter.string(from: startDate)
    }

    private var timeString: String {
        let formatter = DateFormatter()
        formatter.dateFormat = "h:mm a"
        return formatter.string(from: startDate)
    }

    private var stepsString: String {
        "\(session.steps) Steps"
    }

    var body: some View {
        HStack {
            if synced {
                Image(systemName: "checkmark.square.fill")
                    .foregroundColor(.green)
            }
            VStack(alignment: .leading) {
                Text(dateString)
                    .font(.footnote)
                    .foregroundColor(.secondary)
                Text(timeString)
                    .font(.footnote)
                    .foregroundColor(.secondary)
            }
            Spacer()
            Text(stepsString)
                .font(.headline)
                .fontWeight(.bold)
        }
        .padding(4)
    }
}

// MARK: - Session Data Model

struct Session: Identifiable {
    let id = UUID()
    let start: UInt32
    let stop: UInt32
    let steps: UInt32

    var displayString: String {
        // For debugging
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

// MARK: - BLE ViewModel

class BLEViewModel: NSObject, ObservableObject {
    @Published var statusMessage: String = "Idle"
    @Published var sessions: [Session] = []
    @Published var isFetching: Bool = false
    @Published var healthKitSyncStatusMessage: String = ""
    @Published var healthKitSyncCompleted: Bool = false

    private var didReceiveStopMarker: Bool = false
    private var centralManager: CBCentralManager!
    private var peripheral: CBPeripheral?

    private var dataCharacteristic: CBCharacteristic?
    private var confirmCharacteristic: CBCharacteristic?

    // Treadmill Service/Characteristics
    private let serviceUUID = CBUUID(string: "0000A51A-12BB-C111-1337-00099AACDEF0")
    private let dataCharUUID = CBUUID(string: "0000A51A-12BB-C111-1337-00099AACDEF1")
    private let confirmCharUUID = CBUUID(string: "0000A51A-12BB-C111-1337-00099AACDEF2")

    // NEW TIME CHARACTERSITICS
    private let timeReadCharUUID  = CBUUID(string: "0000A51A-12BB-C111-1337-00099AACDEF3")
    private let timeWriteCharUUID = CBUUID(string: "0000A51A-12BB-C111-1337-00099AACDEF4")
    private var timeReadCharacteristic: CBCharacteristic?
    private var timeWriteCharacteristic: CBCharacteristic?

    // Temporary storage for fetched sessions
    private var fetchedSessions: [Session] = []

    // HealthKit
    private let healthStore = HKHealthStore()
    private let stepType = HKObjectType.quantityType(forIdentifier: .stepCount)!
    // 1) Add walkingStepLength as a HKQuantityType
    private let walkingStepLengthType = HKObjectType.quantityType(forIdentifier: .walkingStepLength)!

    private var didDiscoverDevice = false

    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: nil)
    }

    // MARK: - Public Methods

    func fetchAndSaveSessions() {
        // Reset previous states
        healthKitSyncStatusMessage = ""
        healthKitSyncCompleted = false
        didReceiveStopMarker = false

        guard centralManager.state == .poweredOn else {
            statusMessage = "Bluetooth not ready."
            return
        }
        isFetching = true
        statusMessage = "Scanning for TreadSpan Chip..."
        fetchedSessions.removeAll()
        sessions.removeAll()
        didDiscoverDevice = false

        centralManager.scanForPeripherals(withServices: [serviceUUID], options: nil)

        // If no device is discovered after 10s, stop scanning
        DispatchQueue.main.asyncAfter(deadline: .now() + 10) {
            self.centralManager.stopScan()
            if !self.didDiscoverDevice && self.peripheral == nil {
                self.statusMessage = "No TreadSpan Devices Found."
                self.isFetching = false
            }
        }
    }

    // 12-byte session packets
    private func processSessionPacket(_ data: Data) {
        guard data.count == 12 else {
            print("Invalid data length (expected 12).")
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

        let ackData = Data([0x01])  // Acknowledge
        peripheral.writeValue(ackData, for: confirmChar, type: .withResponse)
        print("Attempting to write 0x01 to confirmCharacteristic...")
    }

    private func handleDoneMarker() {
        didReceiveStopMarker = true
        isFetching = false

        if fetchedSessions.isEmpty {
            statusMessage = "No Sessions to Sync."
        } else {
            statusMessage = "All sessions retrieved."
            // Automatically push to HealthKit since we have sessions
            saveToHealthKit()
        }

        print("Done marker received from peripheral. Disconnecting...")
        if let peripheral = peripheral {
            centralManager.cancelPeripheralConnection(peripheral)
        }
    }

    // MARK: - HealthKit

    func saveToHealthKit() {
        guard HKHealthStore.isHealthDataAvailable() else {
            print("Health data not available on this device.")
            healthKitSyncStatusMessage = "Health data not available."
            return
        }

        // 2) Include stride-length in read types
        let writeTypes: Set<HKSampleType> = [stepType] // We'll only write steps
        let readTypes: Set<HKObjectType> = [stepType, walkingStepLengthType]

        if #available(iOS 12.0, *) {
            healthStore.getRequestStatusForAuthorization(toShare: writeTypes, read: readTypes) { [weak self] status, error in
                guard let self = self else { return }

                if let error = error {
                    print("Error checking HK authorization status: \(error.localizedDescription)")
                    DispatchQueue.main.async {
                        self.healthKitSyncStatusMessage = "HealthKit authorization error."
                    }
                    return
                }

                switch status {
                case .shouldRequest:
                    print("User has not seen the HealthKit permission prompt yet.")
                    self.requestHKAuthorization(writeTypes, readTypes: readTypes)
                case .unnecessary:
                    print("HealthKit authorization already granted. Saving sessions...")
                    self.saveAllSessionsAsSteps()
                case .unknown:
                    print("HealthKit authorization status unknown, requesting anyway.")
                    self.requestHKAuthorization(writeTypes, readTypes: readTypes)
                @unknown default:
                    print("New authorization status not handled; requesting anyway.")
                    self.requestHKAuthorization(writeTypes, readTypes: readTypes)
                }
            }
        } else {
            requestHKAuthorization(writeTypes, readTypes: readTypes)
        }
    }

    private func requestHKAuthorization(_ writeTypes: Set<HKSampleType>, readTypes: Set<HKObjectType>) {
        healthStore.requestAuthorization(toShare: writeTypes, read: readTypes) { [weak self] (success, error) in
            guard let self = self else { return }

            if let error = error {
                print("HK authorization error: \(error.localizedDescription)")
                DispatchQueue.main.async {
                    self.healthKitSyncStatusMessage = "HealthKit authorization error."
                }
                return
            }
            if !success {
                print("User did not grant HealthKit authorization.")
                DispatchQueue.main.async {
                    self.healthKitSyncStatusMessage = "HealthKit authorization denied."
                }
                return
            }
            self.saveAllSessionsAsSteps()
        }
    }

    private func saveAllSessionsAsSteps() {
        guard !sessions.isEmpty else {
            print("No sessions to save.")
            DispatchQueue.main.async {
                self.healthKitSyncStatusMessage = "No sessions to save."
            }
            return
        }

        let dispatchGroup = DispatchGroup()
        var successCount = 0

        for session in sessions {
            dispatchGroup.enter()
            let startDate = Date(timeIntervalSince1970: TimeInterval(session.start))
            let endDate   = Date(timeIntervalSince1970: TimeInterval(session.stop))
            let quantity  = HKQuantity(unit: .count(), doubleValue: Double(session.steps))

            let stepSample = HKQuantitySample(
                type: stepType,
                quantity: quantity,
                start: startDate,
                end: endDate
            )

            healthStore.save(stepSample) { success, error in
                if success {
                    print("Saved \(session.steps) steps to HealthKit: \(startDate) - \(endDate)")
                    successCount += 1
                } else {
                    print("Failed saving steps: \(String(describing: error?.localizedDescription))")
                }
                dispatchGroup.leave()
            }
        }

        dispatchGroup.notify(queue: .main) {
            if successCount == self.sessions.count {
                self.healthKitSyncStatusMessage = "Successfully saved all sessions to HealthKit."
                self.healthKitSyncCompleted = true
            } else {
                self.healthKitSyncStatusMessage = "Some sessions failed to sync."
            }
            // 3) Once sessions are saved, fetch stride-length data:
            self.fetchAndPrintStrideLengths()
        }
    }

    // 4) Fetch stride-length samples (last 7 days) and print to console
    private func fetchAndPrintStrideLengths() {
        let calendar = Calendar.current
        let now = Date()
        guard let startDate = calendar.date(byAdding: .day, value: -7, to: now) else {
            return
        }
        let predicate = HKQuery.predicateForSamples(withStart: startDate, end: now, options: .strictStartDate)

        let query = HKSampleQuery(sampleType: walkingStepLengthType,
                                  predicate: predicate,
                                  limit: HKObjectQueryNoLimit,
                                  sortDescriptors: nil) { _, samples, error in
            if let error = error {
                print("Error fetching stride length samples: \(error.localizedDescription)")
                return
            }
            guard let quantitySamples = samples as? [HKQuantitySample], !quantitySamples.isEmpty else {
                print("No stride length samples in the last 7 days.")
                return
            }

            for sample in quantitySamples {
                let strideValue = sample.quantity.doubleValue(for: HKUnit.meter())
                print("Stride length: \(strideValue) m from \(sample.startDate) to \(sample.endDate)")
            }
        }
        healthStore.execute(query)
    }
}

// MARK: - CBCentralManagerDelegate

extension BLEViewModel: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            statusMessage = "Ready to sync"
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
        didDiscoverDevice = true
        centralManager.stopScan()

        self.peripheral = peripheral
        self.peripheral?.delegate = self
        centralManager.connect(peripheral, options: nil)

        statusMessage = "Connecting to \(peripheral.name ?? "Treadmill")..."
    }

    func centralManager(_ central: CBCentralManager,
                        didConnect peripheral: CBPeripheral) {
        statusMessage = "Connected to \(peripheral.name ?? "Treadmill")"
        print("CentralManager: connected to \(peripheral.name ?? "(no name)")")

        peripheral.discoverServices([serviceUUID])
    }

    func centralManager(_ central: CBCentralManager,
                        didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        statusMessage = "Failed to connect."
        self.peripheral = nil
        isFetching = false
        print("Connection failed: \(error?.localizedDescription ?? "Unknown error")")
    }

    func centralManager(_ central: CBCentralManager,
                        didDisconnectPeripheral peripheral: CBPeripheral,
                        error: Error?) {
        print("Disconnected: \(error?.localizedDescription ?? "no error")")
        self.peripheral = nil
        isFetching = false

        // Only update the status if we haven't already received a stop marker.
        if !didReceiveStopMarker && sessions.isEmpty {
            statusMessage = "Disconnected. Please try again."
        }
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
            print("Found TreadSpan service, discovering characteristics...")
            peripheral.discoverCharacteristics([dataCharUUID, confirmCharUUID,
                                               timeReadCharUUID, timeWriteCharUUID],
                                              for: service)
        }
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        if let error = error {
            print("Error discovering characteristics: \(error.localizedDescription)")
            statusMessage = "Error discovering characteristics."
            return
        }
        guard let characteristics = service.characteristics else { return }

        for characteristic in characteristics {
            if characteristic.uuid == dataCharUUID {
                dataCharacteristic = characteristic
            } else if characteristic.uuid == confirmCharUUID {
                confirmCharacteristic = characteristic
                print("Confirmation characteristic found.")
            } else if characteristic.uuid == timeReadCharUUID {
                timeReadCharacteristic = characteristic
                peripheral.readValue(for: characteristic)
                print("Time Read characteristic found. Reading device time...")
            } else if characteristic.uuid == timeWriteCharUUID {
                timeWriteCharacteristic = characteristic
                print("Time Write characteristic found.")
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didUpdateNotificationStateFor characteristic: CBCharacteristic,
                    error: Error?) {
        if let error = error {
            print("Error updating notification state for \(characteristic.uuid): \(error.localizedDescription)")
            return
        }
        if characteristic.isNotifying {
            print("Notifications enabled for \(characteristic.uuid)")
        } else {
            print("Notifications disabled for \(characteristic.uuid)")
        }
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didUpdateValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        if let error = error {
            print("Error updating value for \(characteristic.uuid): \(error.localizedDescription)")
            return
        }
        guard let value = characteristic.value else {
            print("No value for \(characteristic.uuid)")
            return
        }

        // NEW: Handle device time read
        if characteristic.uuid == timeReadCharUUID {
            let deviceEpoch = value.withUnsafeBytes { $0.load(as: UInt32.self).bigEndian }
            let phoneEpoch = UInt32(Date().timeIntervalSince1970)
            let diff = Int(phoneEpoch) - Int(deviceEpoch)
            print("Device time difference (seconds): \(diff)")
            let absDiff = abs(diff)
            if absDiff > 300 {
                let minutesOff = absDiff / 60
                self.statusMessage = "Device time is off by \(minutesOff) minutes. Session times may be inaccurate."
            }
            // Write phone time
            if let timeWriteChar = timeWriteCharacteristic {
                var phoneEpochBigEndian = phoneEpoch.bigEndian
                let data = Data(bytes: &phoneEpochBigEndian, count: 4)
                peripheral.writeValue(data, for: timeWriteChar, type: .withResponse)
                print("Wrote current time to device.")
            }

            // Now subscribe to the data characteristic after reading the time
            if let dataChar = dataCharacteristic {
                peripheral.setNotifyValue(true, for: dataChar)
                statusMessage = "Fetching sessions..."
                print("Data characteristic found. Enabling notifications/indications.")
            }
        }

        // If we see 0xFF => done (stop) marker
        if characteristic.uuid == dataCharUUID {
            if value.count == 1 && value[0] == 0xFF {
                handleDoneMarker()
            } else if value.count == 12 {
                print("Received indicated data: \(value.map { String(format: "%02X", $0) }.joined())")
                processSessionPacket(value)
                confirmSession()
                statusMessage = "Session acknowledged, waiting for next..."
            } else {
                print("Received unknown data length = \(value.count)")
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didWriteValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        if let error = error {
            print("Error writing value for \(characteristic.uuid): \(error.localizedDescription)")
        } else {
            print("Successfully wrote to \(characteristic.uuid)")
        }
    }
}
