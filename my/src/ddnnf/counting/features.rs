use rug::Float;
use std::error::Error;

use super::super::Ddnnf;

impl Ddnnf {
    #[inline]
    /// Computes the cardinality of features for all features in a model.
    /// The results are saved in the file_path. The .csv ending always gets added to the user input.
    /// The function exclusively uses the marking based function.
    /// Here the number of threads influence the speed by using a shared work queue.
    /// # Example
    /// ```
    /// extern crate ddnnf_lib;
    pub fn card_of_each_feature(&mut self, file_path: &str) -> Result<(), Box<dyn Error>> {
        self.annotate_partial_derivatives();

        // start the csv writer with the file_path
        let mut wtr = csv::Writer::from_path(file_path)?;

        for work in 1_i32..self.number_of_variables as i32 + 1 {
            let cardinality = self.card_of_feature_with_partial_derivatives(work);
            wtr.write_record(vec![
                work.to_string(),
                cardinality.to_string(),
                format!("{:.20}", Float::with_val(200, cardinality) / self.rc()),
            ])?;
        }

        Ok(())
    }
}
