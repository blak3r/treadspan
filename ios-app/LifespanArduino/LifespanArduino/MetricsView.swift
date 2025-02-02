import SwiftUI
import HealthKit
import Charts

// MARK: - Range Enum

enum ProChartRange: String, CaseIterable {
    case day = "Day"
    case week = "Week"
    case month = "Month"
    case sixMonth = "6M"
    case year = "Year"
}

// MARK: - Data Models

struct ProSlice: Identifiable {
    let id = UUID()
    let date: Date
    let source: String
    let steps: Double
}

struct ProSelection: Identifiable {
    let id = UUID()
    let date: Date
    let totalSteps: Double
}

// MARK: - ViewModel

class MetricsProViewModel: ObservableObject {
    private let healthStore = HKHealthStore()
    private let stepType = HKQuantityType.quantityType(forIdentifier: .stepCount)!
    
    @Published var chartRange: ProChartRange = .week
    @Published var anchorDate = Date()    // Current reference date
    @Published var slices: [ProSlice] = []
    @Published var selected: ProSelection? = nil
    @Published var isLoading = false
    
    private var didRequestAuth = false
    
    func onAppear() {
        if !didRequestAuth {
            didRequestAuth = true
            requestAuthorization()
        } else {
            fetchAllData()
        }
    }
    
    func setRange(_ range: ProChartRange) {
        chartRange = range
        anchorDate = Date()  // Typically reset to "now" in Apple Health style
        fetchAllData()
    }
    
    func tapBar(at date: Date) {
        // Sum the “Lifespan Arduino” + “All Other” slices for that date bucket
        let bucketSlices = slices.filter { sameBucket($0.date, date) }
        let total = bucketSlices.reduce(0) { $0 + $1.steps }
        if !bucketSlices.isEmpty {
            selected = ProSelection(date: date, totalSteps: total)
        }
    }
    
    func goLeft() {
        anchorDate = shiftRange(date: anchorDate, left: true)
        fetchAllData()
    }
    
    func goRight() {
        anchorDate = shiftRange(date: anchorDate, left: false)
        fetchAllData()
    }
    
    // MARK: - HealthKit
    
    private func requestAuthorization() {
        let toRead: Set<HKObjectType> = [stepType]
        healthStore.requestAuthorization(toShare: [], read: toRead) { success, error in
            DispatchQueue.main.async {
                if success {
                    self.fetchAllData()
                } else {
                    print("HealthKit authorization denied or failed: \(error?.localizedDescription ?? "")")
                }
            }
        }
    }
    
    private func fetchAllData() {
        isLoading = true
        slices = []
        selected = nil
        
        let group = DispatchGroup()
        
        var totalMap: [Date: Double] = [:]
        var deviceMap: [Date: Double] = [:]
        
        let (start, end, interval) = dateIntervalForRange(chartRange, anchor: anchorDate)
        
        // 1) All sources
        group.enter()
        fetchSteps(start: start, end: end, interval: interval, sourceName: nil) { map in
            totalMap = map
            group.leave()
        }
        
        // 2) Lifespan Arduino
        group.enter()
        fetchSteps(start: start, end: end, interval: interval, sourceName: "Lifespan Arduino") { map in
            deviceMap = map
            group.leave()
        }
        
        group.notify(queue: .main) {
            self.buildSlices(totalMap: totalMap, deviceMap: deviceMap)
            self.isLoading = false
        }
    }
    
    private func fetchSteps(start: Date,
                            end: Date,
                            interval: DateComponents,
                            sourceName: String?,
                            completion: @escaping ([Date: Double]) -> Void) {
        var predicate = HKQuery.predicateForSamples(withStart: start, end: end, options: .strictStartDate)
        
        if let name = sourceName {
            let srcQuery = HKSourceQuery(sampleType: stepType, samplePredicate: nil) {
                [weak self] _, sourcesOrNil, error in
                guard let self = self, let sources = sourcesOrNil, error == nil else {
                    completion([:])
                    return
                }
                if let devSrc = sources.first(where: { $0.name == name }) {
                    let sPred = HKQuery.predicateForObjects(from: devSrc)
                    predicate = NSCompoundPredicate(andPredicateWithSubpredicates: [predicate, sPred])
                } else {
                    // Not found
                    completion([:])
                    return
                }
                self.executeStatsQuery(start: start, end: end, interval: interval, predicate: predicate, completion: completion)
            }
            healthStore.execute(srcQuery)
        } else {
            executeStatsQuery(start: start, end: end, interval: interval, predicate: predicate, completion: completion)
        }
    }
    
    private func executeStatsQuery(start: Date,
                                   end: Date,
                                   interval: DateComponents,
                                   predicate: NSPredicate,
                                   completion: @escaping ([Date: Double]) -> Void) {
        let query = HKStatisticsCollectionQuery(quantityType: stepType,
                                                quantitySamplePredicate: predicate,
                                                options: .cumulativeSum,
                                                anchorDate: start,
                                                intervalComponents: interval)
        
        query.initialResultsHandler = { _, results, error in
            guard let statsCollection = results, error == nil else {
                completion([:])
                return
            }
            
            var map: [Date: Double] = [:]
            statsCollection.enumerateStatistics(from: start, to: end) { stats, _ in
                if let sum = stats.sumQuantity() {
                    map[stats.startDate] = sum.doubleValue(for: .count())
                }
            }
            completion(map)
        }
        
        healthStore.execute(query)
    }
    
    private func buildSlices(totalMap: [Date: Double], deviceMap: [Date: Double]) {
        let allKeys = Set(totalMap.keys).union(deviceMap.keys).sorted()
        var final: [ProSlice] = []
        
        for date in allKeys {
            let total = totalMap[date] ?? 0
            let device = deviceMap[date] ?? 0
            let other = max(total - device, 0)
            
            final.append(ProSlice(date: date, source: "Lifespan Arduino", steps: device))
            final.append(ProSlice(date: date, source: "All Other Sources", steps: other))
        }
        slices = final
    }
    
    // MARK: - Date Range Logic
    
    private func dateIntervalForRange(_ range: ProChartRange, anchor: Date)
    -> (Date, Date, DateComponents)
    {
        let cal = Calendar.current
        
        switch range {
        case .day:
            let startOfDay = cal.startOfDay(for: anchor)
            guard let nextDay = cal.date(byAdding: .day, value: 1, to: startOfDay) else {
                return (startOfDay, anchor, DateComponents(hour: 1))
            }
            return (startOfDay, nextDay, DateComponents(hour: 1))
            
        case .week:
            let dayAnchor = cal.startOfDay(for: anchor)
            guard let start = cal.date(byAdding: .day, value: -6, to: dayAnchor) else {
                return (dayAnchor, dayAnchor, DateComponents(day: 1))
            }
            return (start, dayAnchor, DateComponents(day: 1))
            
        case .month:
            let dayAnchor = cal.startOfDay(for: anchor)
            guard let start = cal.date(byAdding: .day, value: -29, to: dayAnchor) else {
                return (dayAnchor, dayAnchor, DateComponents(day: 1))
            }
            return (start, dayAnchor, DateComponents(day: 1))
            
        case .sixMonth:
            let monthStart = cal.date(bySetting: .day, value: 1, of: anchor) ?? anchor
            guard let earliest = cal.date(byAdding: .month, value: -5, to: monthStart) else {
                return (monthStart, anchor, DateComponents(month: 1))
            }
            return (earliest, monthStart, DateComponents(month: 1))
            
        case .year:
            let monthStart = cal.date(bySetting: .day, value: 1, of: anchor) ?? anchor
            guard let earliest = cal.date(byAdding: .month, value: -11, to: monthStart) else {
                return (monthStart, anchor, DateComponents(month: 1))
            }
            return (earliest, monthStart, DateComponents(month: 1))
        }
    }
    
    private func shiftRange(date: Date, left: Bool) -> Date {
        let cal = Calendar.current
        let delta = left ? -1 : 1
        
        switch chartRange {
        case .day:
            return cal.date(byAdding: .day, value: delta, to: date) ?? date
        case .week:
            return cal.date(byAdding: .day, value: 7 * delta, to: date) ?? date
        case .month:
            return cal.date(byAdding: .month, value: delta, to: date) ?? date
        case .sixMonth:
            return cal.date(byAdding: .month, value: 6 * delta, to: date) ?? date
        case .year:
            return cal.date(byAdding: .year, value: delta, to: date) ?? date
        }
    }
    
    private func sameBucket(_ d1: Date, _ d2: Date) -> Bool {
        let cal = Calendar.current
        switch chartRange {
        case .day:
            return d1 == d2
        case .week, .month:
            return cal.isDate(d1, inSameDayAs: d2)
        case .sixMonth, .year:
            let c1 = cal.dateComponents([.year, .month], from: d1)
            let c2 = cal.dateComponents([.year, .month], from: d2)
            return c1.year == c2.year && c1.month == c2.month
        }
    }
}

// MARK: - The View

struct MetricsProView: View {
    @StateObject private var vm = MetricsProViewModel()
    
    // For left/right swipes
    @GestureState private var dragOffset: CGFloat = 0
    
    var body: some View {
        NavigationView {
            VStack(spacing: 0) {
                // Segmented control
                Picker("", selection: $vm.chartRange) {
                    ForEach(ProChartRange.allCases, id: \.self) { mode in
                        Text(mode.rawValue).tag(mode)
                    }
                }
                .pickerStyle(.segmented)
                .padding()
                .onChange(of: vm.chartRange) { newValue in
                    vm.setRange(newValue)
                }
                
                // Selected bar info
                if let sel = vm.selected {
                    VStack {
                        Text(formatSelected(sel.date))
                            .font(.headline)
                        Text("Steps: \(Int(sel.totalSteps))")
                            .font(.subheadline)
                    }
                    .padding(.vertical, 6)
                }
                
                // Main chart area
                if vm.isLoading && vm.slices.isEmpty {
                    ProgressView("Fetching data…")
                        .padding()
                } else if vm.slices.isEmpty {
                    Text("No Data")
                        .padding()
                } else {
                    chartView
                        // Give more horizontal margin
                        .padding(.horizontal, 16)
                        // Some vertical spacing
                        .padding(.bottom, 10)
                        .frame(height: 300)
                        // Left/right swipe
                        .gesture(
                            DragGesture(minimumDistance: 50)
                                .updating($dragOffset) { val, state, _ in
                                    state = val.translation.width
                                }
                                .onEnded { val in
                                    if val.translation.width < 0 {
                                        vm.goLeft()  // older
                                    } else {
                                        vm.goRight() // newer
                                    }
                                }
                        )
                }
                
                Spacer()
            }
            .navigationBarTitle("Metrics Pro", displayMode: .inline)
        }
        .onAppear {
            vm.onAppear()
        }
    }
    
    // MARK: - The Chart
    
    private var chartView: some View {
        Chart(vm.slices) { slice in
            BarMark(
                x: .value("Date", slice.date),
                y: .value("Steps", slice.steps)
            )
            .foregroundStyle(by: .value("Source", slice.source))
            .position(by: .value("Source", slice.source))  // stacked bars
        }
        // In iOS16, no .chartYAxis(.hidden), so we manually hide lines/ticks:
        .chartYAxis {
            AxisMarks { _ in
                AxisGridLine().foregroundStyle(.clear)
                AxisTick().foregroundStyle(.clear)
                AxisValueLabel().foregroundStyle(.clear)
            }
        }
        // Hide x-axis lines/labels
        .chartXAxis {
            AxisMarks(values: xAxisValues()) { _ in
                AxisGridLine().foregroundStyle(.clear)
                AxisTick().foregroundStyle(.clear)
                AxisValueLabel().foregroundStyle(.clear)
            }
        }
        // Tap detection overlay
        .chartOverlay { proxy in
            GeometryReader { geo in
                Rectangle()
                    .fill(.clear)
                    .contentShape(Rectangle())
                    .gesture(
                        DragGesture(minimumDistance: 0)
                            .onEnded { val in
                                // Convert the drag location to chart X
                                let localX = val.location.x - geo[proxy.plotAreaFrame].origin.x
                                if let tappedDate: Date = proxy.value(atX: localX) {
                                    vm.tapBar(at: tappedDate)
                                }
                            }
                    )
            }
        }
        // Slightly lighter background to mimic Apple Health
        .background(Color(UIColor.systemGray6))
        .cornerRadius(8)
    }
    
    // Minimal x-axis values
    private func xAxisValues() -> [Date] {
        let domainDates = vm.slices.map { $0.date }
        return Array(Set(domainDates)).sorted()
    }
    
    // MARK: - Formatting the selected date label
    
    private func formatSelected(_ date: Date) -> String {
        let f = DateFormatter()
        switch vm.chartRange {
        case .day:
            f.dateFormat = "MMM d, h a"
        case .week, .month:
            f.dateFormat = "MMM d"
        case .sixMonth, .year:
            f.dateFormat = "MMM yyyy"
        }
        return f.string(from: date)
    }
}

struct MetricsProView_Previews: PreviewProvider {
    static var previews: some View {
        MetricsProView()
    }
}
