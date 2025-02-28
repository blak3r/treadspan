import SwiftUI

struct FAQItem: Identifiable {
    let id = UUID()
    let question: String
    let answer: AnyView
}

struct FAQView: View {
    @State private var expandedQuestion: UUID?
    
    let faqItems: [FAQItem] = [
        FAQItem(
            question: "Is this app affiliated with LifeSpan Fitness?",
            answer: AnyView(
                Text("No. This app is in no way associated with LifeSpan Fitness. It was built by Blake Robertson (a LifeSpan customer) that wanted a more seamless way to track his steps.")
            )
        ),
        FAQItem(
            question: "Do I need anything else besides the app for it to work?",
            answer: AnyView(
                Text("Yes, in order to use the App you need to have a compatible treadspan device. This is essentially a ESP32 chip you buy off Amazon and load firmware onto.  See [this page](https://www.github.com/blak3r/treadspan) for more information.")
            )
        ),
        FAQItem(
            question: "How do I submit feature requests / support?",
            answer: AnyView(
                Text("Go to the [TreadSpan Discussion Page](https://github.com/blak3r/treadspan/discussions) for feature requests and support.")
            )
        ),
        FAQItem(
            question: "Why do you need READ steps permission?",
            answer: AnyView(
                Text("Read permission is needed for the metrics view to work. Our metrics view is similar to the Apple Health Steps graphs, but adds a data series showing how many steps you take on a treadmill. To do this, we need to read steps data from other sources as well.")
            )
        ),
        FAQItem(
            question: "Is any information shared with 3rd Parties?",
            answer: AnyView(
                Text("Absolutely not. The app does not report any information to 3rd parties. All information is stored in Apple HealthKit.")
            )
        ),
        FAQItem(
            question: "Can I still use LifeSpan Fit?",
            answer: AnyView(
                Text("In order for the Treadspan solution to work, there is a chip that connects to your LifeSpan Omni Console. This chip uses the same Bluetooth connection that the LifeSpan Mobile App uses. As long as the chip is powered on, it will occupy the connection so the original app cannot connect. Once you power down the chip by unplugging its USB-C connector, the Bluetooth icon on the console will go grey or disappear, and you will be able to connect the original LifeSpan app.")
            )
        ),
        FAQItem(
            question: "Which model treadmills are supported?",
            answer: AnyView(
                Text("Currently, the solution has been tested with the [LifeSpan Treadmills such as TR1200](https://amzn.to/4bbn8ok). It should work with any LifeSpan treadmill that works with the Omni Console. If you have the RETRO console, there is also a solution, but the hardware requires some assembly and is probably out of reach if you're not a tinkerer.")
            )
        )
    ]
    
    var body: some View {
        NavigationView {
            List {
                ForEach(faqItems) { item in
                    DisclosureGroup(
                        isExpanded: Binding(
                            get: { expandedQuestion == item.id },
                            set: { expandedQuestion = $0 ? item.id : nil }
                        ),
                        content: {
                            item.answer
                                .padding(.top, 5)
                        },
                        label: {
                            Text(item.question)
                                .font(.headline)
                        }
                    )
                }
            }
            .navigationTitle("FAQ")
        }
    }
}

struct FAQView_Previews: PreviewProvider {
    static var previews: some View {
        FAQView()
    }
}
